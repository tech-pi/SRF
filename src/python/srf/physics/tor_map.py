import tensorflow as tf
from dxl.learn.core import ConfigurableWithName, Tensor
import os
# load op
TF_ROOT = os.environ.get('TENSORFLOW_ROOT')
op = tf.load_op_library(
    TF_ROOT + '/bazel-bin/tensorflow/core/user_ops/tof.so')


class ToRMapModel(ConfigurableWithName):
    class KEYS:
        KERNEL_WIDTH = 'kernel_width'

    def __init__(self, name, *, kernel_width=None, config=None):
        config = self._parse_input_config(config, {
            self.KEYS.KERNEL_WIDTH: kernel_width
        })
        super().__init__(name, config)

    @classmethod
    def _default_config(self):
        return {
            self.KEYS.KERNEL_WIDTH: 1.0
        }
    AXIS = ('x', 'y', 'z')

    def perm(self, axis):
        if axis == 'z':
            return [2, 1, 0]
        if axis == 'y':
            return [1, 2, 0]
        if axis == 'x':
            return [0, 2, 1]

    def rotate_param(self, value, axis):
        return [value[p] for p in self.perm(axis)]

    def check_inputs(self, data, name):
        if not isinstance(data, dict):
            raise TypeError(
                "{} should be dict, got {}.".format(name, data))
        for a in self.AXIS:
            if not a in data:
                raise ValueError("{} missing axis {}.".format(name, a))

    def backprojection(self, lors, image):
        lors_values = lors['lors_value']
        lors = lors['lors']
        lors = lors.transpose()
        result = Tensor(op.backprojection_gpu(
            image=tf.transpose(image.data),
            grid=image.grid,
            center=image.center,
            size=image.size,
            lors=lors.data,
            line_integral=lors_values.data,
            kernel_width=self.config(self.KEYS.KERNEL_WIDTH)))
        return result