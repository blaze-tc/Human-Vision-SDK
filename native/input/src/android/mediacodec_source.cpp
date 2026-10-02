#include "input_internal.h"
#include "android_input_gpu.h"
#include <android/log.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <memory>
#include <cstring>
#include <unistd.h>
namespace hvinput {
static std::atomic<int> live_images{0}, live_ahb_references{0}, live_owned_fds{0};
// No pixel mapping or sampling occurs during Task5. Returning the original
// acquire fence as release fence preserves producer completion even on failure.
AndroidDecodedImage::~AndroidDecodedImage() {
  const int fd=acquire_fd;
  if(image) { AImage_deleteAsync(image,acquire_fd); acquire_fd=-1; }
  else if(acquire_fd>=0) close(acquire_fd);
  if(buffer) AHardwareBuffer_release(buffer);
  if(image_counted) {--live_images;if(fd>=0)--live_owned_fds;}
  if(buffer) --live_ahb_references;
  if(image) __android_log_print(ANDROID_LOG_INFO,"HVInputGate","decoded_lease_returned=true acquire_fd=%d release_fence=original_acquire no_gpu_work=true ahb_reference_released=%d active_images=%d active_ahb_references=%d active_owned_fds=%d",fd,buffer!=nullptr,live_images.load(),live_ahb_references.load(),live_owned_fds.load());
}
static std::mutex gate_mutex;
static std::unique_ptr<Session> gate;
static std::string codec_name;
static void SafeLog(void*,int,const char*,va_list) {}
void InstallSafeLog() {
  static std::once_flag once;
  std::call_once(once,[]{av_log_set_callback(SafeLog); avformat_network_init();});
}
struct Resources {
  AVFormatContext* format=avformat_alloc_context();
  AVPacket* packet=av_packet_alloc();
  AImageReader* reader=nullptr;
  AMediaCodec* codec=nullptr;
  AMediaFormat* configuration=nullptr;
  bool started=false;
  ~Resources() {
    if(codec) { if(started) AMediaCodec_stop(codec); AMediaCodec_delete(codec); }
    if(configuration) AMediaFormat_delete(configuration);
    if(reader) AImageReader_delete(reader);
    av_packet_free(&packet); avformat_close_input(&format);
    __android_log_print(ANDROID_LOG_INFO,"HVInputGate","decoder_resources_closed=true active_images=%d active_ahb_references=%d active_owned_fds=%d",live_images.load(),live_ahb_references.load(),live_owned_fds.load());
  }
};
void Session::Decode() {
  Resources r;
  auto fail=[this](const char* stage,int code){SetError(stage,code); state=HV_INPUT_FAILED;
    __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","capability_result=FAIL stage=%s code=%d",stage,code); stop=true;};
  if(!tcp) {fail("Android capability gate requires RTSP TCP",AVERROR(EINVAL));return;}
  if(!r.format || !r.packet) {fail("allocate compressed demux",AVERROR(ENOMEM));return;}
  r.format->interrupt_callback={Interrupt,this};
  AVDictionary* options=nullptr; av_dict_set(&options,"rtsp_transport","tcp",0);
  deadline=NowUs()+int64_t(timeout_ms)*1000;
  int status=avformat_open_input(&r.format,url.c_str(),nullptr,&options); av_dict_free(&options);
  if(status<0) {fail("open compressed RTSP",status);return;}
  // RTSP SDP already declares streams/extradata; never call find_stream_info,
  // avcodec_open or receive_frame, which may decode CPU frames while probing.
  int stream=-1;
  for(unsigned i=0;i<r.format->nb_streams;++i) if(r.format->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) {stream=i;break;}
  if(stream<0 || r.format->streams[stream]->codecpar->codec_id!=AV_CODEC_ID_H264) {fail("RTSP stream must be H264",AVERROR_INVALIDDATA);return;}
  auto* params=r.format->streams[stream]->codecpar;
  ANativeWindow* window=nullptr;
  status=AImageReader_newWithUsage(max_width,max_height,AIMAGE_FORMAT_PRIVATE,AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,4,&r.reader);
  if(status!=AMEDIA_OK || AImageReader_getWindow(r.reader,&window)!=AMEDIA_OK) {fail("PRIVATE GPU sampled ImageReader",status);return;}
  std::string name;
  {std::lock_guard<std::mutex> lock(gate_mutex);name=codec_name;}
  if(name.empty()) {fail("hardware codec capability selection missing",AVERROR(EINVAL));return;}
  r.codec=AMediaCodec_createCodecByName(name.c_str());
  r.configuration=AMediaFormat_new();
  if(!r.codec || !r.configuration) {fail("create selected hardware AVC codec",AVERROR_DECODER_NOT_FOUND);return;}
  AMediaFormat_setString(r.configuration,AMEDIAFORMAT_KEY_MIME,"video/avc");
  AMediaFormat_setInt32(r.configuration,AMEDIAFORMAT_KEY_WIDTH,max_width);
  AMediaFormat_setInt32(r.configuration,AMEDIAFORMAT_KEY_HEIGHT,max_height);
  if(params->extradata_size>0) AMediaFormat_setBuffer(r.configuration,"csd-0",params->extradata,params->extradata_size);
  status=AMediaCodec_configure(r.codec,r.configuration,window,nullptr,0);
  if(status!=AMEDIA_OK) {fail("configure hardware PRIVATE Surface",status);return;}
  status=AMediaCodec_start(r.codec);
  if(status!=AMEDIA_OK) {fail("start hardware decoder",status);return;} r.started=true;
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","hardware_codec=%s requested_width=%u requested_height=%u csd_bytes=%d generation=%llu",name.c_str(),max_width,max_height,params->extradata_size,(unsigned long long)info.generation);
  int decoded=0;
  auto drain=[&] {
    AMediaCodecBufferInfo output{};
    for(int count=0;count<16 && !stop;++count) {
      auto index=AMediaCodec_dequeueOutputBuffer(r.codec,&output,0);
      if(index==AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        auto* actual=AMediaCodec_getOutputFormat(r.codec);
        __android_log_print(ANDROID_LOG_INFO,"HVInputGate","actual_codec_format=%s",AMediaFormat_toString(actual));
        AMediaFormat_delete(actual); continue;
      }
      if(index<0) break;
      auto release=AMediaCodec_releaseOutputBuffer(r.codec,index,true);
      if(release!=AMEDIA_OK) {fail("render hardware output Surface",release);return;}
    }
    AndroidDecodedImage image;
    int acquired=AImageReader_acquireLatestImageAsync(r.reader,&image.image,&image.acquire_fd);
    if(acquired==AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) return;
    if(acquired!=AMEDIA_OK) {fail("acquireLatestImageAsync",acquired);return;}
    image.image_counted=true; ++live_images;if(image.acquire_fd>=0)++live_owned_fds;
    AHardwareBuffer* borrowed=nullptr;
    if(AImage_getHardwareBuffer(image.image,&borrowed)!=AMEDIA_OK || !borrowed) {fail("decoded AHB missing",AVERROR_INVALIDDATA);return;}
    AHardwareBuffer_acquire(borrowed); image.buffer=borrowed;++live_ahb_references;
    int64_t ns=0; AImage_getTimestamp(image.image,&ns);
    image.generation=info.generation; image.pts_us=ns/1000;
    image.received_us=info.received_timestamp_us; image.decoded_us=NowUs();
    RecordDecodedCapability(image,name.c_str()); ++decoded;
  };
  while(!stop && decoded<3) {
    deadline=NowUs()+int64_t(timeout_ms)*1000;
    status=av_read_frame(r.format,r.packet); info.received_timestamp_us=NowUs();
    if(status<0) {fail("read compressed H264",status);return;}
    if(r.packet->stream_index!=stream) {av_packet_unref(r.packet);continue;}
    ssize_t index=-1;
    while(!stop && NowUs()<deadline && (index=AMediaCodec_dequeueInputBuffer(r.codec,1000))<0) drain();
    if(stop) return;
    if(index<0) {fail("bounded compressed queue pressure",AVERROR(ETIMEDOUT));return;}
    size_t capacity=0; auto* data=AMediaCodec_getInputBuffer(r.codec,index,&capacity);
    if(!data || r.packet->size<0 || size_t(r.packet->size)>capacity) {fail("compressed access unit exceeds codec buffer",AVERROR(ENOBUFS));return;}
    std::memcpy(data,r.packet->data,r.packet->size);
    int64_t pts=r.packet->pts==AV_NOPTS_VALUE?0:av_rescale_q(r.packet->pts,r.format->streams[stream]->time_base,AVRational{1,1000000});
    status=AMediaCodec_queueInputBuffer(r.codec,index,0,r.packet->size,pts,0); av_packet_unref(r.packet);
    if(status!=AMEDIA_OK) {fail("submit hardware compressed H264",status);return;}
    drain();
  }
  if(decoded) state=HV_INPUT_STREAMING;
  stop=true; // bounded capability probe, no Task6 GPU import/publication.
}
void StartCapabilityProbe(const char* url) {
  std::lock_guard<std::mutex> lock(gate_mutex);
  if(gate || !url || std::strncmp(url,"rtsp://",7)) return;
  gate=std::make_unique<Session>(); gate->url=url; gate->tcp=true;
  gate->max_width=640; gate->max_height=360; gate->timeout_ms=5000;
  gate->info.generation=1; InstallSafeLog();
  gate->worker=std::thread(&Session::Run,gate.get());
}
void StopCapabilityProbe() {
  std::unique_ptr<Session> retiring;
  {std::lock_guard<std::mutex> lock(gate_mutex);retiring=std::move(gate);}
  if(retiring) {retiring->stop=true;retiring->wake.notify_all();if(retiring->worker.joinable())retiring->worker.join();}
}
}
extern "C" __attribute__((visibility("default"))) void HV_Input_SelectHardwareCodec(const char* name) {
  std::lock_guard<std::mutex> lock(hvinput::gate_mutex);
  hvinput::codec_name=name?name:"";
}
