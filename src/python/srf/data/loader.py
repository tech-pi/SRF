from dxl.learn.core import ConfigurableWithName, Constant
import numpy as np
from dxl.data.io import load_npz


class MasterLoader:
    def __init__(self, shape):
        self.shape = shape

    def load(self, target_graph):
        # return Constant(np.ones(self.shape, dtype=np.float32), 'x_init')
        x = np.ones(self.shape, dtype=np.float32)
        x = x / np.sum(x) * 1058478
        return x.astype(np.float32)


class WorkerLoader:
    def __init__(self, lors_path, emap_path):
        self.lors_path = lors_path
        self.emap_path = emap_path

    def load(self, target_graph):
        lors = load_npz(self.lors_path)
        lors = {
            a: Constant(lors[a].astype(np.float32), 'lors_{}'.format(a))
            for a in ['x', 'y', 'z']
        }
        emap = np.load(self.emap_path).astype(np.float32)
        emap = Constant(emap, 'emap')
        return {'projection_data': lors, 'efficiency_map': emap}, ()


class OSEMWorkerLoader:
    pass