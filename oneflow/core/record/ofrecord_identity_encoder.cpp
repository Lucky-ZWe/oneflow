#include "oneflow/core/record/ofrecord_identity_encoder.h"
#include "oneflow/core/record/record.h"

namespace oneflow {

namespace {

template<typename T>
decltype(std::declval<T>().mutable_value()) GetMutFeatureDataList(Feature& feature);

#define SPECIALIZE_GET_MUT_FEATURE_DATA_LIST(T, type_proto, data_list)                      \
  template<>                                                                                \
  decltype(std::declval<T>().mutable_value()) GetMutFeatureDataList<T>(Feature & feature) { \
    return feature.mutable_##data_list();                                                   \
  }
OF_PP_FOR_EACH_TUPLE(SPECIALIZE_GET_MUT_FEATURE_DATA_LIST, FEATURE_DATA_TYPE_FEATURE_FIELD_SEQ);

}  // namespace

template<typename T>
void OFRecordEncoderImpl<EncodeCase::kIdentity, T>::EncodeOneCol(
    DeviceCtx* ctx, const Blob* in_blob, int64_t in_offset, Feature& feature,
    const std::string& field_name, int64_t one_col_elem_num) const {
  const T& data = in_blob->dptr<T>()[in_offset];
  CheckRecordValue<T>(data);
  *GetMutFeatureDataList<T>(feature) = data.value();
}

#define INSTANTIATE_OFRECORD_IDENTITY_ENCODER(type_cpp, type_proto) \
  template class OFRecordEncoderImpl<EncodeCase::kIdentity, type_cpp>;
OF_PP_FOR_EACH_TUPLE(INSTANTIATE_OFRECORD_IDENTITY_ENCODER, FEATURE_DATA_TYPE_SEQ);

}  // namespace oneflow
