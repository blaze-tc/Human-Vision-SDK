#include "gpu/android/gpu_parity_image.h"
#if defined(__ANDROID__) && defined(HV_ANDROID_R4_PARITY)
#include "gpu/android/gpu_parity_fixture_data.h"
#include <android/log.h>
#include <cstring>
#include <cmath>

namespace humanvision::gpu {
ImageParityReduction::~ImageParityReduction() {
    if(!device_) return;
    if(pipeline_) vkDestroyPipeline(device_,pipeline_,nullptr);
    if(fault_pipeline_) vkDestroyPipeline(device_,fault_pipeline_,nullptr);
    if(fault_layout_) vkDestroyPipelineLayout(device_,fault_layout_,nullptr);
    if(fault_set_layout_) vkDestroyDescriptorSetLayout(device_,fault_set_layout_,nullptr);
    if(fault_view_) vkDestroyImageView(device_,fault_view_,nullptr);
    if(fault_image_) vkDestroyImage(device_,fault_image_,nullptr);
    if(fault_memory_) vkFreeMemory(device_,fault_memory_,nullptr);
    if(layout_) vkDestroyPipelineLayout(device_,layout_,nullptr);
    if(pool_) vkDestroyDescriptorPool(device_,pool_,nullptr);
    if(set_layout_) vkDestroyDescriptorSetLayout(device_,set_layout_,nullptr);
    if(sampler_) vkDestroySampler(device_,sampler_,nullptr);
    for(size_t i=0;i<buffers_.size();i++) {
        if(mapped_[i]) vkUnmapMemory(device_,memories_[i]);
        if(buffers_[i]) vkDestroyBuffer(device_,buffers_[i],nullptr);
        if(memories_[i]) vkFreeMemory(device_,memories_[i],nullptr);
    }
}
bool ImageParityReduction::Initialize(VkPhysicalDevice physical,VkDevice device,const std::array<uint8_t,16>& device_uuid) {
    fixture_=CurrentParityFixture(); if(!fixture_||device_) return false;
    device_=device; device_uuid_=device_uuid;
    for(int i=0;i<2;i++) { contexts_[i]={this,uint32_t(i)};probes_[i]=std::make_unique<GpuParityProbe>(ParityDispatch{&contexts_[i],DispatchRecord,DispatchCollect}); }
    VkPhysicalDeviceMemoryProperties props{}; vkGetPhysicalDeviceMemoryProperties(physical,&props);
    for(size_t i=0;i<buffers_.size();i++) {
        const size_t bytes=i==0?fixture_->bytes[0].size():sizeof(ParityReduction);
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size=bytes;
        info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        if(vkCreateBuffer(device_,&info,nullptr,&buffers_[i])!=VK_SUCCESS) return false;
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device_,buffers_[i],&req);
        uint32_t type=UINT32_MAX;
        for(uint32_t j=0;j<props.memoryTypeCount;j++)
            if((req.memoryTypeBits&(1u<<j)) && (props.memoryTypes[j].propertyFlags &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { type=j; break; }
        if(type==UINT32_MAX) return false;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=req.size; alloc.memoryTypeIndex=type;
        if(vkAllocateMemory(device_,&alloc,nullptr,&memories_[i])!=VK_SUCCESS ||
            vkBindBufferMemory(device_,buffers_[i],memories_[i],0)!=VK_SUCCESS ||
            vkMapMemory(device_,memories_[i],0,bytes,0,&mapped_[i])!=VK_SUCCESS) return false;
        if(i==0) std::memcpy(mapped_[0],fixture_->bytes[0].data(),bytes);
    }
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(vkCreateSampler(device_,&sampler,nullptr,&sampler_)!=VK_SUCCESS) return false;
    VkDescriptorSetLayoutBinding bindings[3]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; set.bindingCount=3; set.pBindings=bindings;
    if(vkCreateDescriptorSetLayout(device_,&set,nullptr,&set_layout_)!=VK_SUCCESS) return false;
    VkDescriptorPoolSize sizes[3]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,20},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,36},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets=20; pool.poolSizeCount=3; pool.pPoolSizes=sizes;
    if(vkCreateDescriptorPool(device_,&pool,nullptr,&pool_)!=VK_SUCCESS) return false;
    std::array<VkDescriptorSetLayout,18> layouts; layouts.fill(set_layout_);
    VkDescriptorSetAllocateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; sets.descriptorPool=pool_; sets.descriptorSetCount=18; sets.pSetLayouts=layouts.data();
    if(vkAllocateDescriptorSets(device_,&sets,sets_.data())!=VK_SUCCESS) return false;
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,48};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; layout.setLayoutCount=1; layout.pSetLayouts=&set_layout_; layout.pushConstantRangeCount=1; layout.pPushConstantRanges=&push;
    if(vkCreatePipelineLayout(device_,&layout,nullptr,&layout_)!=VK_SUCCESS) return false;
    std::vector<uint32_t> spirv;
    if(!CompileParityImageShader(spirv)) return false;
    VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; module_info.codeSize=spirv.size()*4; module_info.pCode=spirv.data();
    VkShaderModule module=VK_NULL_HANDLE;
    if(vkCreateShaderModule(device_,&module_info,nullptr,&module)!=VK_SUCCESS) return false;
    VkComputePipelineCreateInfo pipe{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; pipe.layout=layout_;
    pipe.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
    const auto result=vkCreateComputePipelines(device_,VK_NULL_HANDLE,1,&pipe,nullptr,&pipeline_);
    vkDestroyShaderModule(device_,module,nullptr);
    return result==VK_SUCCESS && InitializeFaults(physical);
}
bool ImageParityReduction::InitializeFaults(VkPhysicalDevice physical) {
    VkFormatProperties format{};vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,&format);
    if((format.optimalTilingFeatures&(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))!=
        (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return false;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;
    image.format=VK_FORMAT_R8G8B8A8_UNORM;image.extent={fixture_->width,fixture_->height,1};
    image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    if(vkCreateImage(device_,&image,nullptr,&fault_image_)!=VK_SUCCESS)return false;
    VkMemoryRequirements req{};vkGetImageMemoryRequirements(device_,fault_image_,&req);
    VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(physical,&props);
    uint32_t type=UINT32_MAX;
    for(uint32_t i=0;i<props.memoryTypeCount;i++)if(req.memoryTypeBits&(1u<<i)){type=i;if(props.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)break;}
    if(type==UINT32_MAX)return false;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;
    if(vkAllocateMemory(device_,&allocation,nullptr,&fault_memory_)!=VK_SUCCESS||vkBindImageMemory(device_,fault_image_,fault_memory_,0)!=VK_SUCCESS)return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=fault_image_;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if(vkCreateImageView(device_,&view,nullptr,&fault_view_)!=VK_SUCCESS)return false;
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=2;set.pBindings=bindings;
    if(vkCreateDescriptorSetLayout(device_,&set,nullptr,&fault_set_layout_)!=VK_SUCCESS)return false;
    VkDescriptorSetLayout layouts[2]={fault_set_layout_,fault_set_layout_};
    VkDescriptorSetAllocateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sets.descriptorPool=pool_;sets.descriptorSetCount=2;sets.pSetLayouts=layouts;
    if(vkAllocateDescriptorSets(device_,&sets,fault_sets_.data())!=VK_SUCCESS)return false;
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,12};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&fault_set_layout_;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
    if(vkCreatePipelineLayout(device_,&layout,nullptr,&fault_layout_)!=VK_SUCCESS)return false;
    const char* shader=R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(binding=0) uniform sampler2D source_image;
layout(binding=1,rgba8) uniform writeonly image2D fault_image;
layout(push_constant) uniform parameter { uint width;uint height;uint mode; } p;
void main() {
    uvec2 q=gl_GlobalInvocationID.xy;if(q.x>=p.width||q.y>=p.height)return;
    uint plane=p.width*p.height,pixel=q.y*p.width+q.x;vec4 result;
    for(uint c=0u;c<4u;c++) {
        uint j=pixel,k=c;
        if(p.mode==1u&&c<3u)k=2u-c;
        if(p.mode==2u)j=(p.height-1u-q.y)*p.width+q.x;
        if(p.mode==4u)j=(q.y*(p.width-1u)+q.x)%plane;
        if(p.mode==5u){uint wrong=pixel*4u+c;j=wrong%plane;k=wrong/plane;}
        float value=texelFetch(source_image,ivec2(j%p.width,j/p.width),0)[k];
        if(p.mode==3u&&q.x>=p.width*3u/4u)value=0.0;
        if(p.mode==6u)value*=.5;
        if(p.mode==7u)value=1.0-value;
        result[c]=value;
    }
    imageStore(fault_image,ivec2(q),result);
}
)glsl";
    std::vector<uint32_t> spirv;if(!CompileParityShaderSource(shader,spirv))return false;
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=spirv.size()*4;info.pCode=spirv.data();
    VkShaderModule module=VK_NULL_HANDLE;if(vkCreateShaderModule(device_,&info,nullptr,&module)!=VK_SUCCESS)return false;
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.layout=fault_layout_;
    pipeline.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
    const auto result=vkCreateComputePipelines(device_,VK_NULL_HANDLE,1,&pipeline,nullptr,&fault_pipeline_);
    vkDestroyShaderModule(device_,module,nullptr);return result==VK_SUCCESS;
}
void ImageParityReduction::RecordReduction(VkCommandBuffer command,VkImageView view,VkImageLayout image_layout,uint32_t index) {
    const uint32_t constants[12]={fixture_->width,fixture_->height,4,fixture_->width*4,1,4,0,fixture_->width*4,1,4,0,0x3f800000u};
    VkDescriptorImageInfo image{sampler_,view,image_layout};
    VkDescriptorBufferInfo golden{buffers_[0],0,fixture_->bytes[0].size()},summary{buffers_[index+1],0,sizeof(ParityReduction)};
    VkWriteDescriptorSet writes[3]{};
    for(int j=0;j<3;j++){writes[j].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[j].dstSet=sets_[index];writes[j].dstBinding=j;writes[j].descriptorCount=1;
        writes[j].descriptorType=j==0?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;}
    writes[0].pImageInfo=&image;writes[1].pBufferInfo=&golden;writes[2].pBufferInfo=&summary;
    vkUpdateDescriptorSets(device_,3,writes,0,nullptr);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout_,0,1,&sets_[index],0,nullptr);
    vkCmdPushConstants(command,layout_,VK_SHADER_STAGE_COMPUTE_BIT,0,48,constants);vkCmdDispatch(command,1,1,1);
}
void ImageParityReduction::RecordFaults(VkCommandBuffer command) {
    // One full-image control per completed producer submission avoids a long
    // uninterrupted diagnostic burst that can trigger the device watchdog.
    if(next_fault_>=16 || faults_pending_) return;
    const uint32_t stage=next_fault_/8,mode=next_fault_%8;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    barrier.srcAccessMask=next_fault_?VK_ACCESS_SHADER_READ_BIT:0;
    barrier.oldLayout=next_fault_?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=fault_image_;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(command,next_fault_?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkDescriptorImageInfo source{sampler_,views_[stage],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},target{VK_NULL_HANDLE,fault_view_,VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[2]{};
        for(uint32_t i=0;i<2;i++){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=fault_sets_[stage];writes[i].dstBinding=i;writes[i].descriptorCount=1;
            writes[i].descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=i?&target:&source;}
        vkUpdateDescriptorSets(device_,2,writes,0,nullptr);
            const uint32_t push[]={fixture_->width,fixture_->height,mode};
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,fault_pipeline_);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,fault_layout_,0,1,&fault_sets_[stage],0,nullptr);
            vkCmdPushConstants(command,fault_layout_,VK_SHADER_STAGE_COMPUTE_BIT,0,12,push);
            vkCmdDispatch(command,(fixture_->width+7)/8,(fixture_->height+7)/8,1);
            barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            RecordReduction(command,fault_view_,VK_IMAGE_LAYOUT_GENERAL,2+stage*8+mode);
    faults_pending_=true;
}
bool ImageParityReduction::DispatchRecord(void* opaque,const ParityGpuView&,const ParityGpuView&,
    const ParityContract& contract,uint32_t,ParityReduction&) noexcept {
    auto& context=*static_cast<DispatchContext*>(opaque);auto& self=*context.owner;const int i=int(context.index);
    if(!self.command_||!self.views_[i]) return false;
    const uint32_t constants[12]={self.fixture_->width,self.fixture_->height,4,self.fixture_->width*4,1,4,0,self.fixture_->width*4,1,4,0,0x3f800000u};
        VkDescriptorImageInfo image{self.sampler_,self.views_[i],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo golden{self.buffers_[0],0,self.fixture_->bytes[0].size()},summary{self.buffers_[i+1],0,sizeof(ParityReduction)};
        VkWriteDescriptorSet writes[3]{};
        for(int j=0;j<3;j++) { writes[j].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[j].dstSet=self.sets_[i]; writes[j].dstBinding=j; writes[j].descriptorCount=1;
            writes[j].descriptorType=j==0?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; }
        writes[0].pImageInfo=&image; writes[1].pBufferInfo=&golden; writes[2].pBufferInfo=&summary;
        vkUpdateDescriptorSets(self.device_,3,writes,0,nullptr);
        vkCmdBindPipeline(self.command_,VK_PIPELINE_BIND_POINT_COMPUTE,self.pipeline_);
        vkCmdBindDescriptorSets(self.command_,VK_PIPELINE_BIND_POINT_COMPUTE,self.layout_,0,1,&self.sets_[i],0,nullptr);
        vkCmdPushConstants(self.command_,self.layout_,VK_SHADER_STAGE_COMPUTE_BIT,0,48,constants);
        vkCmdDispatch(self.command_,1,1,1);
    return contract.element_tolerance==1.0f;
}
bool ImageParityReduction::DispatchCollect(void* opaque,uint32_t,ParityReduction& reduction) noexcept {
    auto& context=*static_cast<DispatchContext*>(opaque);auto& self=*context.owner;
    if(!self.completed_proof_) return false;
    std::memcpy(&reduction,self.mapped_[context.index+1],sizeof(reduction));return true;
}
bool ImageParityReduction::Record(VkCommandBuffer command,VkImage source,VkImageView source_view,VkImageLayout source_layout,
    VkImage producer,VkImageView producer_view,VkImageLayout producer_layout,uint64_t generation,uint64_t source_id,uint32_t slot) {
    if(!pipeline_||!source_view||!producer_view||pending_) return false;
    if(completed_>=2 && (slot!=0 || next_fault_>=16) &&
       fixture_->import_controls.IsComplete()) return true;
    VkImageMemoryBarrier barriers[2]{};
    VkImage images[2]={source,producer}; VkImageLayout previous[2]={source_layout,producer_layout};
    for(int i=0;i<2;i++) {
        auto& b=barriers[i]; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        b.oldLayout=previous[i]; b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=images[i]; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    }
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,2,barriers);
    command_=command;views_={source_view,producer_view};completed_proof_=false;
    for(int i=0;i<2;i++) {
        ParityGpuView actual{};actual.resource=reinterpret_cast<uintptr_t>(images[i]);actual.device_uuid=device_uuid_;
        actual.generation=generation;actual.source_id=source_id;actual.width=fixture_->width;actual.height=fixture_->height;
        actual.channels=4;actual.row_stride_elements=fixture_->width*4;actual.channel_stride_elements=1;
        actual.dtype=ParityDtype::Unorm8;actual.elempack=1;
        auto golden=actual;golden.resource=reinterpret_cast<uintptr_t>(buffers_[0]);
        golden.identity=ParityIdentity::IndependentGolden;golden.content_sha256=ParityHashBytes(fixture_->hashes[0]);
        ParityContract contract{};contract.golden_sha256=golden.content_sha256;contract.generation=generation;contract.source_id=source_id;
        contract.width=actual.width;contract.height=actual.height;contract.channels=4;contract.dtype=actual.dtype;contract.elempack=1;
        contract.element_tolerance=contract.max_error_limit=contract.mean_error_limit=1.0f;
        if(!probes_[i]->Record(i==0?ParityStage::Source:ParityStage::Producer,actual,golden,contract,tickets_[i]))return false;
    }
    if(next_fault_<16 && slot==0) RecordFaults(command);
    for(int i=0;i<2;i++) { auto& b=barriers[i]; b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT; b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT; b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; b.newLayout=previous[i]; }
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
    generation_=generation; source_=source_id; slot_=slot; pending_=true; return true;
}
void ImageParityReduction::ReportCompleted() {
    if(!pending_) return;
    completed_proof_=true;
    bool clean[2]={false,false};
    for(int i=0;i<2;i++) {
        ParitySummary summary{};
        if(!probes_[i]->TryCollect(tickets_[i],summary)) {
            __android_log_print(ANDROID_LOG_ERROR,"HV_R4_PARITY","reduction_ticket_error stage=%s",i==0?"source":"producer");continue;
        }
        ReportParitySummary(i==0?"source":"producer",summary,*fixture_,slot_,0,reinterpret_cast<uintptr_t>(device_));
        clean[i]=summary.passed;
    }
    if(faults_pending_) {
        const char* names[]={"clean","channel_swap","vertical_flip","truncated_copy","wrong_stride","wrong_packing","wrong_scale","old_slot_content"};
        const uint32_t stage=next_fault_/8,mode=next_fault_%8;
            ParityReduction r{};std::memcpy(&r,mapped_[3+stage*8+mode],sizeof(r));
            const bool pass=clean[0]&&clean[stage]&&r.element_count==fixture_->width*fixture_->height*4&&
                (mode==0?(r.mismatch_count==0&&r.max_error<=1.f):(r.mismatch_count>0&&r.first_mismatch<r.element_count&&r.max_error>1.f));
            __android_log_print(ANDROID_LOG_INFO,"HV_R4_PARITY",
                "gpu_boundary_control stage=%s name=%s pass=%d generation=%llu source_id=%llu slot=%u prior_clean=%d baseline_clean=%d count=%u mismatches=%u first=%u max=%.9g",
                stage==0?"source":"producer",names[mode],pass,(unsigned long long)generation_,(unsigned long long)source_,slot_,clean[0],clean[stage],r.element_count,r.mismatch_count,r.first_mismatch,r.max_error);
        ++next_fault_;
        faults_pending_=false;
    }
    pending_=false;
    ++completed_;
}
}
#endif
