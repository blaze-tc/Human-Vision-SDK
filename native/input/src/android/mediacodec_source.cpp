#include "input_internal.h"
#include "android_input_gpu.h"
#include "input_vulkan_private.h"
#include "input_h264_color.h"
#include "input_frame_ring.h"
#include <android/log.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <memory>
#include <cstring>
#include <unistd.h>
#include <new>
#include <utility>
#include <dlfcn.h>
#include <limits>
namespace hvinput {
static std::atomic<int> live_images{0}, live_ahb_references{0}, live_owned_fds{0};
// No pixel mapping or sampling occurs during Task5. Returning the original
// acquire fence as release fence preserves producer completion even on failure.
AndroidDecodedImage::~AndroidDecodedImage() {
  const int fd=acquire_fd;const int returned_release_fd=release_fd;
  if(gpu_submitted&&acquire_fd>=0)close(acquire_fd);
  if(image) { AImage_deleteAsync(image,gpu_submitted?release_fd:acquire_fd); acquire_fd=-1;release_fd=-1; }
  else if(acquire_fd>=0) close(acquire_fd);
  if(buffer) AHardwareBuffer_release(buffer);
  if(image_counted) {--live_images;if(fd>=0)--live_owned_fds;}
  if(buffer) --live_ahb_references;
  if(release_fd_counted)--live_owned_fds;
  if(release_fd_counted)__android_log_print(ANDROID_LOG_INFO,"HVInputGate","release_fd_transferred_to_AImage=%d active_owned_fds=%d",returned_release_fd,live_owned_fds.load());
  if(image) __android_log_print(ANDROID_LOG_INFO,"HVInputGate","decoded_lease_returned=true acquire_fd=%d release_fence=%s gpu_fence_complete=%d no_gpu_work=%d ahb_reference_released=%d active_images=%d active_ahb_references=%d active_owned_fds=%d",fd,gpu_submitted?"actual_gpu_release":"original_acquire",gpu_submitted,!gpu_submitted,buffer!=nullptr,live_images.load(),live_ahb_references.load(),live_owned_fds.load());
}
void AndroidDecodedImage::CountReleaseFd() noexcept {if(release_fd>=0&&!release_fd_counted){release_fd_counted=true;++live_owned_fds;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","release_fd_held=%d active_owned_fds=%d",release_fd,live_owned_fds.load());}}
void AndroidDecodedImage::Reset() noexcept {this->~AndroidDecodedImage();new(this)AndroidDecodedImage();}
void AndroidDecodedImage::TakeFrom(AndroidDecodedImage& s) noexcept {
 image=std::exchange(s.image,nullptr);buffer=std::exchange(s.buffer,nullptr);acquire_fd=std::exchange(s.acquire_fd,-1);image_counted=std::exchange(s.image_counted,false);
 release_fd=std::exchange(s.release_fd,-1);release_fd_counted=std::exchange(s.release_fd_counted,false);gpu_submitted=std::exchange(s.gpu_submitted,false);generation=s.generation;pts_us=s.pts_us;received_us=s.received_us;decoded_us=s.decoded_us;
 matrix=s.matrix;color_range=s.color_range;transfer=s.transfer;primaries=s.primaries;width=s.width;height=s.height;crop_left=s.crop_left;crop_top=s.crop_top;crop_right=s.crop_right;crop_bottom=s.crop_bottom;
}
static std::mutex gate_mutex;
static std::unique_ptr<HV_InputSessionOpaque> gate;
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
    DrainColorImagesBeforeReaderClose();
    if(codec) { if(started) AMediaCodec_stop(codec); AMediaCodec_delete(codec); }
    if(configuration) AMediaFormat_delete(configuration);
    if(reader) AImageReader_delete(reader);
    av_packet_free(&packet); avformat_close_input(&format);
    __android_log_print(ANDROID_LOG_INFO,"HVInputGate","decoder_resources_closed=true active_images=%d active_ahb_references=%d active_owned_fds=%d",live_images.load(),live_ahb_references.load(),live_owned_fds.load());
  }
};
void Session::Decode() {
  const bool production=InputGpuProduction(this);
  if(production)PrepareInputGpuGeneration(this);
  Resources r;
  auto fail=[this](const char* stage,int code){SetError(stage,code); state=HV_INPUT_FAILED;
    __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","capability_result=FAIL stage=%s code=%d",stage,code); stop=true;};
  if(!tcp) {fail("Android capability gate requires RTSP TCP",AVERROR(EINVAL));return;}
  if(!r.format || !r.packet) {fail("allocate compressed demux",AVERROR(ENOMEM));return;}
  r.format->interrupt_callback={Interrupt,this};
  AVDictionary* options=nullptr; av_dict_set(&options,"rtsp_transport","tcp",0);
  deadline=NowUs()+int64_t(timeout_ms)*1000;
  int status=avformat_open_input(&r.format,url.c_str(),nullptr,&options); av_dict_free(&options);
  if(status<0) {SetError("open compressed RTSP",status);state=HV_INPUT_RECONNECTING;if(!production)stop=true;return;}
  // RTSP SDP already declares streams/extradata; never call find_stream_info,
  // avcodec_open or receive_frame, which may decode CPU frames while probing.
  int stream=-1;
  for(unsigned i=0;i<r.format->nb_streams;++i) if(r.format->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) {stream=i;break;}
  if(stream<0 || r.format->streams[stream]->codecpar->codec_id!=AV_CODEC_ID_H264) {fail("RTSP stream must be H264",AVERROR_INVALIDDATA);return;}
  auto* params=r.format->streams[stream]->codecpar;
  H264Color declared{};bool valid_sps=ParseH264Color(params->extradata,params->extradata_size>0?size_t(params->extradata_size):0,declared);
  if((ColorProbeEnabled()||production)&&params->extradata_size>0&&!valid_sps){fail("H264 SPS color contract malformed or unsupported",AVERROR_INVALIDDATA);return;}
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","source_h264_vui valid_sps=%d present=%d matrix=%u range=%u matrix_code=%u primaries=%u transfer=%u demux_av_colorspace=%d demux_av_range=%d metadata_domain=H264_SPS_VUI",valid_sps,declared.present,declared.matrix,declared.range,declared.matrix_code,declared.primaries,declared.transfer,params->color_space,params->color_range);
  ANativeWindow* window=nullptr;
  status=AImageReader_newWithUsage(max_width,max_height,AIMAGE_FORMAT_PRIVATE,AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,4,&r.reader);
  if(status!=AMEDIA_OK || AImageReader_getWindow(r.reader,&window)!=AMEDIA_OK) {fail("PRIVATE GPU sampled ImageReader",status);return;}
  if(ColorProbeEnabled()||production) { AImageReader_BufferRemovedListener listener{nullptr,[](void*,AImageReader*,AHardwareBuffer* b){NotifyRemovedBuffer(b);}};AImageReader_setBufferRemovedListener(r.reader,&listener); }
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
  uint32_t matrix=declared.matrix,range=declared.range,transfer=0,primaries=0;
  AImageCropRect codec_crop{};bool codec_crop_known=false;
  auto drain=[&] {
    AMediaCodecBufferInfo output{};
    for(int count=0;count<16 && !stop;++count) {
      auto index=AMediaCodec_dequeueOutputBuffer(r.codec,&output,0);
      if(index==AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        auto* actual=AMediaCodec_getOutputFormat(r.codec);
        codec_crop_known=false;codec_crop={}; // Never retain a stale declaration after a format change.
        // getRect is optional API28; API26 never imports that symbol strongly.
        using GetRect=bool(*)(AMediaFormat*,const char*,int32_t*,int32_t*,int32_t*,int32_t*);
        static auto get_rect=reinterpret_cast<GetRect>(dlsym(RTLD_DEFAULT,"AMediaFormat_getRect"));
        int32_t left=0,top=0,right=0,bottom=0;
        bool got=get_rect&&get_rect(actual,"crop",&left,&top,&right,&bottom);
        if(!got)got=AMediaFormat_getInt32(actual,"crop-left",&left)&&AMediaFormat_getInt32(actual,"crop-top",&top)&&AMediaFormat_getInt32(actual,"crop-right",&right)&&AMediaFormat_getInt32(actual,"crop-bottom",&bottom);
        if(got&&left>=0&&top>=0&&right>=left&&bottom>=top&&right<std::numeric_limits<int32_t>::max()&&bottom<std::numeric_limits<int32_t>::max()){codec_crop={left,top,right+1,bottom+1};codec_crop_known=true;}
        __android_log_print(ANDROID_LOG_INFO,"HVInputGate","actual_codec_crop known=%d left=%d top=%d right_exclusive=%d bottom_exclusive=%d optional_api28_getRect=%d",codec_crop_known,codec_crop.left,codec_crop.top,codec_crop.right,codec_crop.bottom,get_rect!=nullptr);
        int32_t value=0;
        if(AMediaFormat_getInt32(actual,"color-standard",&value)){auto known=value==1?2u:(value==2||value==4?1u:0u);primaries=uint32_t(value);if(known&&declared.matrix&&known!=declared.matrix){AMediaFormat_delete(actual);fail("decoder matrix conflicts with H264 VUI",AVERROR_INVALIDDATA);return;}matrix=known?known:declared.matrix;}
        if(AMediaFormat_getInt32(actual,"color-range",&value)){auto known=value==1?1u:(value==2?2u:0u);if(known&&declared.range&&known!=declared.range){AMediaFormat_delete(actual);fail("decoder range conflicts with H264 VUI",AVERROR_INVALIDDATA);return;}range=known?known:declared.range;}
        if(AMediaFormat_getInt32(actual,"color-transfer",&value))transfer=uint32_t(value); // Android codes retained; no sRGB assumption
        __android_log_print(ANDROID_LOG_INFO,"HVInputGate","actual_codec_format=%s",AMediaFormat_toString(actual));
        __android_log_print(ANDROID_LOG_INFO,"HVInputGate","resolved_color matrix=%u range=%u vui_matrix=%u vui_range=%u android_raw_standard=%u android_raw_transfer=%u vui_primaries=%u vui_transfer=%u unknown_output_color_space=0",matrix,range,declared.matrix,declared.range,primaries,transfer,declared.primaries,declared.transfer);
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
    image.matrix=matrix;image.color_range=range;image.transfer=transfer;image.primaries=primaries;
    AImage_getWidth(image.image,&image.width);AImage_getHeight(image.image,&image.height);AImageCropRect crop{};
    if(AImage_getCropRect(image.image,&crop)!=AMEDIA_OK){fail("query actual decoder crop",AVERROR_INVALIDDATA);return;}
    AHardwareBuffer_Desc allocation{};AHardwareBuffer_describe(image.buffer,&allocation);
    if((ColorProbeEnabled()||production)&&(!codec_crop_known||codec_crop.right>int64_t(allocation.width)||codec_crop.bottom>int64_t(allocation.height)||codec_crop.right-codec_crop.left!=image.width||codec_crop.bottom-codec_crop.top!=image.height)){fail("actual decoder crop unavailable or incompatible with AHB/display geometry",AVERROR_INVALIDDATA);return;}
    const auto actual_crop=(ColorProbeEnabled()||production)?codec_crop:crop;
    image.crop_left=actual_crop.left;image.crop_top=actual_crop.top;image.crop_right=actual_crop.right;image.crop_bottom=actual_crop.bottom;
    if(ColorProbeEnabled()||production)__android_log_print(ANDROID_LOG_INFO,"HVInputGate","resolved_ahb_crop left=%d top=%d right_exclusive=%d bottom_exclusive=%d ahb_width=%u ahb_height=%u aimage_rect=%d,%d,%d,%d crop_source=MediaCodec_coded_rect",actual_crop.left,actual_crop.top,actual_crop.right,actual_crop.bottom,allocation.width,allocation.height,crop.left,crop.top,crop.right,crop.bottom);
    if(ColorProbeEnabled()||production)__android_log_print(ANDROID_LOG_INFO,"HVInputGate","decoded_crop generation=%llu image_width=%d image_height=%d left=%d top=%d right=%d bottom=%d display_width=%d display_height=%d matrix=%u range=%u transfer=%u primaries=%u crop_metadata_domain=AImage_full_window matrix_range_domain=resolved_Android_or_H264_VUI raw_transfer_primaries_domain=AndroidMediaFormat",(unsigned long long)image.generation,image.width,image.height,crop.left,crop.top,crop.right,crop.bottom,crop.right-crop.left,crop.bottom-crop.top,matrix,range,transfer,primaries);
    if(!production||decoded==0)RecordDecodedCapability(image,name.c_str()); ++decoded;
    if(production)state=HV_INPUT_STREAMING;
    if(ColorProbeEnabled()||production)QueueColorImage(image);
  };
  InputEncodedBacklog backlog;backlog.Reset(NowUs()+int64_t(timeout_ms)*1000);uint32_t startup_dropped=0;
  // One compressed packet is retained. Pressure cannot accumulate an unbounded application queue.
  // On decoder pressure, recreate RTSP/decoder resources, then require a new keyframe.
  const auto reconnect=[&](const char* stage,int code){SetError(stage,code);state=HV_INPUT_RECONNECTING;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","encoded_backlog_controlled_reconnect=1 wait_for_keyframe=1 stage=%s",stage);};
  while(!stop && (production||ColorProbeEnabled()||decoded<3)) {
    deadline=NowUs()+int64_t(timeout_ms)*1000;
    status=av_read_frame(r.format,r.packet); info.received_timestamp_us=NowUs();
    if(status<0) {if(production)reconnect("read compressed H264",status);else fail("read compressed H264",status);return;}
    if(r.packet->stream_index!=stream) {av_packet_unref(r.packet);continue;}
    // A newly joined RTSP stream may begin inside a GOP. Concealed decoder output
    // is not a valid source image: start the hardware decoder input at a keyframe.
    bool was_waiting=backlog.Waiting();
    if(!backlog.Admit((r.packet->flags&AV_PKT_FLAG_KEY)!=0)){++startup_dropped;av_packet_unref(r.packet);if(backlog.Expired(NowUs())){fail("RTSP startup keyframe not received within configured timeout",AVERROR(ETIMEDOUT));return;}continue;}
    if(was_waiting)__android_log_print(ANDROID_LOG_INFO,"HVInputGate","decoder_first_keyframe=true compressed_startup_packets_dropped=%u packet_flags=%d packet_pts=%lld",startup_dropped,r.packet->flags,(long long)r.packet->pts);
    ssize_t index=-1;
    while(!stop && NowUs()<deadline && (index=AMediaCodec_dequeueInputBuffer(r.codec,1000))<0) drain();
    if(stop) return;
    if(index<0) {if(production)reconnect("bounded compressed queue pressure",AVERROR(ETIMEDOUT));else fail("bounded compressed queue pressure",AVERROR(ETIMEDOUT));return;}
    size_t capacity=0; auto* data=AMediaCodec_getInputBuffer(r.codec,index,&capacity);
    if(!data || r.packet->size<0 || size_t(r.packet->size)>capacity) {fail("compressed access unit exceeds codec buffer",AVERROR(ENOBUFS));return;}
    std::memcpy(data,r.packet->data,r.packet->size);
    int64_t pts=r.packet->pts==AV_NOPTS_VALUE?0:av_rescale_q(r.packet->pts,r.format->streams[stream]->time_base,AVRational{1,1000000});
    status=AMediaCodec_queueInputBuffer(r.codec,index,0,r.packet->size,pts,0); av_packet_unref(r.packet);
    if(status!=AMEDIA_OK) {fail("submit hardware compressed H264",status);return;}
    drain();
  }
  if(decoded) state=HV_INPUT_STREAMING;
  if(!production)stop=true; // Only the diagnostic is bounded.
}
void StartCapabilityProbe(const char* url) {
  std::lock_guard<std::mutex> lock(gate_mutex);
  if(gate || !url || std::strncmp(url,"rtsp://",7)) return;
  gate=std::make_unique<HV_InputSessionOpaque>(); gate->url=url; gate->tcp=true;
  gate->max_width=640; gate->max_height=360; gate->timeout_ms=5000;
  gate->info.generation=1; InstallSafeLog();
  gate->worker=std::thread(&Session::Run,gate.get());
}
void StopCapabilityProbe() {
  std::unique_ptr<HV_InputSessionOpaque> retiring;
  {std::lock_guard<std::mutex> lock(gate_mutex);retiring=std::move(gate);}
  if(retiring) {retiring->stop=true;retiring->wake.notify_all();if(retiring->worker.joinable())retiring->worker.join();}
}
}
extern "C" __attribute__((visibility("default"))) void HV_Input_SelectHardwareCodec(const char* name) {
  std::lock_guard<std::mutex> lock(hvinput::gate_mutex);
  hvinput::codec_name=name?name:"";
}
extern "C" __attribute__((visibility("default"))) HV_InputHandle HV_Input_GetCapabilityProbeHandle(){std::lock_guard<std::mutex> lock(hvinput::gate_mutex);return hvinput::gate.get();}

// Diagnostic shutdown is requested without joining. The render coroutine keeps
// polling actual GPU completion while the worker releases its reader resources.
extern "C" __attribute__((visibility("default"))) void HV_Input_RequestCapabilityProbeStop(){
 std::lock_guard<std::mutex> lock(hvinput::gate_mutex);
 if(hvinput::gate){hvinput::gate->stop=true;hvinput::gate->wake.notify_all();}
}
extern "C" __attribute__((visibility("default"))) int HV_Input_TryFinishCapabilityProbeStop(){
 std::unique_ptr<HV_InputSessionOpaque> retiring;
 {std::lock_guard<std::mutex> lock(hvinput::gate_mutex);
  if(!hvinput::gate)return 1;
  if(!hvinput::gate->worker_done)return 0;
  retiring=std::move(hvinput::gate);
 }
 // worker_done is published only after Decode's resources have been destroyed.
 if(retiring->worker.joinable())retiring->worker.join();
 return 1;
}
