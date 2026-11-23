#include "humanvision_image_service_v1.h"
#include <stddef.h>
_Static_assert(offsetof(HV_ImageServiceApiV1, context)==8,"C table context offset");
_Static_assert(sizeof(((HV_ImageTransformPlanV1*)0)->plan_fingerprint)==8,"fingerprint width");
_Static_assert(sizeof(((HV_ImageIdentityV1*)0)->source_id)==8,"source identity width");
int main(void) {
 HV_PreparedImageLeaseV1 value={0};value.struct_size=sizeof(value);value.api_version=1;
 return value.api_version!=HV_IMAGE_SERVICE_API_V1;
}
