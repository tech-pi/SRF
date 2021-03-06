import abc

import numpy as np

from srf.io.listmode import load_h5
from srf.preprocess.function.on_tor_lors import str2axis, recon_process
from srf.preprocess.merge_map import crop_image
from srf.utils.config import config_with_name
from .image import Image
from .listmode import ListModeData, ListModeDataSplit
from .sinogram_new import SinogramData


class MasterLoader:
    class KEYS:
        GRID = 'grid'
        CENTER = 'center'
        SIZE = 'size'

    def __init__(self, scanner, image_config, name = 'master_loader'):
        self.config = config_with_name(name)
        self.config.update(self.KEYS.GRID, image_config['grid'])
        self.config.update(self.KEYS.CENTER, image_config['center'])
        self.config.update(self.KEYS.SIZE, image_config['size'])
        self._scanner = scanner

    # Todo:
    def create_image(self):
        '''
        this method is temporarily added for RingPET scanner.
        '''
        image = np.ones(self.config[self.KEYS.GRID], dtype = np.float32)
        return crop_image(self._scanner, image,
                          self.config[self.KEYS.GRID],
                          self.config[self.KEYS.CENTER],
                          self.config[self.KEYS.SIZE],
                          0.95)

    def load(self, target_graph):
        image = self.create_image()
        return Image(image,
                     self.config[self.KEYS.CENTER],
                     self.config[self.KEYS.SIZE])


class WorkerLoader(abc.ABC):
    class KEYS:
        LORS_PATH = 'lors_path'
        EMAP_PATH = 'emap_path'
        CENTER = 'center'
        SIZE = 'size'
        TOF_BIN = 'tof_bin'
        TOF_RES = 'tof_res'
        PSF_XY_PATH = 'psf_xy_path'
        PSF_Z_PATH = 'psf_z_path'

    def __init__(self, lors_path, emap_path, scanner, image_config, correction, name = 'worker_loader'):
        KS = self.KEYS 
        self.config = config_with_name(name)
        self.config.update(KS.LORS_PATH, lors_path)
        self.config.update(KS.EMAP_PATH, emap_path)
        self.config.update(KS.CENTER, image_config['center'])
        self.config.update(KS.SIZE, image_config['size'])
        self.config.update(KS.TOF_BIN, scanner.tof_bin)
        self.config.update(KS.TOF_RES, scanner.tof_res)
        if correction.psf is not None:
            self.psf_flag = True
            self.config.update(KS.PSF_XY_PATH, correction.psf.xy)
            self.config.update(KS.PSF_Z_PATH, correction.psf.z)
            print('with psf correction.')
        else:
            self.psf_flag = False
            print('without psf correction.')
    
    def _load_psf(self):
        # TODO: move to doufo
        from dxl.learn.core.tensor import SparseTensor, Constant
        from scipy import sparse
        import scipy.io as scio
        KS = self.KEYS
        KC = self.config
        psf_xy_mat = scio.loadmat(KC[KS.PSF_XY_PATH])
        psf_xy_data = psf_xy_mat['matrix']
        psf_xy = sparse.coo_matrix(psf_xy_data)
        psf_z = np.load(KC[KS.PSF_Z_PATH])
        return SparseTensor(psf_xy, 'psf_xy'), Constant(psf_z, 'psf_z')




    @abc.abstractmethod
    def load(self, target_graph):
        pass


class SplitWorkerLoader(WorkerLoader):
    def load(self, target_graph):
        if self.config[self.KEYS.LORS_PATH].endswith(".npy"):
            lors = np.load(self.config[self.KEYS.LORS_PATH])
        elif self.config[self.KEYS.LORS_PATH].endswith(".h5"):
            data = load_h5(self.config[self.KEYS.LORS_PATH])
            lors_point = np.hstack((data['fst'], data['snd']))
            lors = np.hstack(
                (lors_point, data['tof'].reshape(data['tof'].size, 1)))
        

        # tof_sigma2 = (limit**2)/9
        lors = recon_process(lors, self.config[self.KEYS.TOF_RES])
        lors = {k: lors[str2axis(k)] for k in ('x', 'y', 'z')}
        projection_data = ListModeDataSplit(
            **{k: ListModeData(lors[k], np.ones([lors[k].shape[0]], np.float32)) for k in lors})
        emap = Image(np.load(self.config[self.KEYS.EMAP_PATH]).astype(np.float32),
                     self.config[self.KEYS.CENTER],
                     self.config[self.KEYS.SIZE])
        result = {'projection_data': projection_data, 'efficiency_map': emap}
        if self.psf_flag:
            psf_xy, psf_z = self._load_psf()
            result.update({'psf_xy':psf_xy, 'psf_z':psf_z})
        return result, ()


class CompleteWorkerLoader(WorkerLoader):
    def load(self, target_graph):
        if self.config[self.KEYS.LORS_PATH].endswith(".npy"):
            lors = np.load(self.config[self.KEYS.LORS_PATH])
        elif self.config[self.KEYS.LORS_PATH].endswith(".h5"):
            data = load_h5(self.config[self.KEYS.LORS_PATH])
            lors_point = np.hstack((data['fst'], data['snd']))
            if 'tof' in data.keys():
                lors = np.hstack(
                    (lors_point, data['tof'].reshape(data['tof'].size, 1)))
                projection_data = ListModeData(lors, lors[:, 6])
            else:
                lors = np.hstack((lors_point, np.ones((
                    lors_point.shape[0], 1))))
                projection_data = ListModeData(lors, lors[:, 6])

        emap = Image(np.load(self.config[self.KEYS.EMAP_PATH]).astype(np.float32),
                        self.config[self.KEYS.CENTER],
                        self.config[self.KEYS.SIZE])
        result = {'projection_data': projection_data, 'efficiency_map': emap}
        if self.psf_flag:
            psf_xy, psf_z = self._load_psf()
            result.update({'psf_xy':psf_xy, 'psf_z':psf_z})
        return result, ()


class OSEMWorkerLoader:
    pass


class siddonProjectionLoader(abc.ABC):
    class KEYS:
        LORS_PATH = 'lors_path'
        CENTER = 'center'
        SIZE = 'size'

    def __init__(self, lors_path, center, size, name = 'siddon_loader'):
        self.config = config_with_name(name)
        self.config.update(self.KEYS.LORS_PATH, lors_path)
        self.config.update(self.KEYS.CENTER, center)
        self.config.update(self.KEYS.SIZE, size)

    def load(self, target_graph):
        if self.config[self.KEYS.LORS_PATH].endswith(".npy"):
            lors = np.load(self.config[self.KEYS.LORS_PATH])
        elif self.config[self.KEYS.LORS_PATH].endswith(".h5"):
            data = load_h5(self.config[self.KEYS.LORS_PATH])
            lors_point = np.hstack((data['fst'], data['snd']))
            lors = np.hstack(
                (lors_point, data['weight'].reshape(data['weight'].size, 1)))
        projection_data = ListModeData(
            lors, np.ones([lors.shape[0]], np.float32))
        return {'projection_data': projection_data}, ()


class siddonSinogramLoader(abc.ABC):
    class KEYS:
        PJ_CONFIG = 'pj_config'
        LORS_PATH = 'lors_path'
        EMAP_PATH = 'emap_path'
        CENTER = 'center'
        SIZE = 'size'

    def __init__(self, pj_config, lors_path, emap_path, image_config, name = 'sinogram_loader'):
        self.config = config_with_name(name)
        self.config.update(self.KEYS.PJ_CONFIG, pj_config)
        self.config.update(self.KEYS.LORS_PATH, lors_path)
        self.config.update(self.KEYS.EMAP_PATH, emap_path)
        self.config.update(self.KEYS.CENTER, image_config['center'])
        self.config.update(self.KEYS.SIZE, image_config['size'])

    def load(self, target_graph):
        if self.config[self.KEYS.LORS_PATH].endswith(".npy"):
            lors = np.load(self.config[self.KEYS.LORS_PATH])
        elif self.config[self.KEYS.LORS_PATH].endswith(".h5"):
            data = load_h5(self.config[self.KEYS.LORS_PATH])
            lors_point = np.hstack((data['fst'], data['snd']))
            lors = np.hstack(
                (lors_point, data['weight'].reshape(data['weight'].size, 1)))
        listmode = ListModeData(lors, np.ones([lors.shape[0]], np.float32))
        # sino = np.load('sinogram_data.npy')
        sino = _listMode2Sino(listmode, self.config[self.KEYS.PJ_CONFIG])
        # np.save('sinogram_data.npy', sino)

        emap = Image(np.load(self.config[self.KEYS.EMAP_PATH]).astype(np.float32),
                     self.config[self.KEYS.CENTER],
                     self.config[self.KEYS.SIZE])
        return {'projection_data': sino, 'efficiency_map': emap}, ()


def _listMode2Sino(listmode: ListModeData, pj_config) -> SinogramData:
    ang = np.pi * 2 / 16  # config['ring']['nb_blocks_per_ring']
    x_data1 = listmode.lors[:, 0]
    y_data1 = listmode.lors[:, 1]
    z_data1 = listmode.lors[:, 2]
    angles1 = np.arctan2(y_data1, x_data1) + np.pi
    iblock1 = np.round(angles1 / ang) % 16
    angle = iblock1 * ang
    y_data1o = -x_data1 * np.sin(angle) + y_data1 * np.cos(angle)
    y_ind1 = np.round((y_data1o + 33.4 / 2 - 3.34 / 2) / 3.34)
    z_ind1 = np.round((z_data1 + 33.4 / 2 - 3.34 / 2) / 3.34)

    x_data2 = listmode.lors[:, 3]
    y_data2 = listmode.lors[:, 4]
    z_data2 = listmode.lors[:, 5]
    angles2 = np.arctan2(y_data2, x_data2) + np.pi
    iblock2 = np.round(angles2 / ang) % 16
    angle = iblock2 * ang
    y_data2o = -x_data2 * np.sin(angle) + y_data2 * np.cos(angle)
    y_ind2 = np.round((y_data2o + 33.4 / 2 - 3.34 / 2) / 3.34)
    z_ind2 = np.round((z_data2 + 33.4 / 2 - 3.34 / 2) / 3.34)

    sino = np.zeros((10 * 10 * 16, 10 * 10 * 16), dtype = int)
    print(listmode.lors[0, :])
    ind1 = (y_ind1 + 10 * z_ind1 + 100 * iblock1).astype(int)
    ind2 = (y_ind2 + 10 * z_ind2 + 100 * iblock2).astype(int)

    for i in range(len(x_data1)):
        if i * 100 % len(x_data1) == 0:
            print(i * 100 / len(x_data1))
        sino[ind1[i], ind2[i]] = sino[ind1[i], ind2[i]] + 1
        sino[ind2[i], ind1[i]] = sino[ind2[i], ind1[i]] + 1

    return SinogramData(sino)
