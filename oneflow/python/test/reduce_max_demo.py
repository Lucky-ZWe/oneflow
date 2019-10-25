import oneflow as flow
import numpy as np
import oneflow.core.common.data_type_pb2 as data_type_util
flow.config.gpu_device_num(1)
flow.config.grpc_use_no_signal()
flow.config.default_data_type(flow.float)
@flow.function
def ReduceMaxJob(
    inputs = flow.input_blob_def(
        (10, 10), dtype=data_type_util.kFloat, is_dynamic=True
    ),
):
    return flow.math.reduce_max(inputs, axis=[0])
inputs = np.array([[1, 2], [3, 1]]).astype(np.float32)
out = ReduceMaxJob(inputs).get()
print(out)