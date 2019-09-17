import oneflow as flow

flow.config.gpu_device_num(1)
flow.config.grpc_use_no_signal()

@flow.function
def ConstantScalarJob():
    return dlnet.ConstantScalar(3.14)

print(ConstantScalarJob().get())