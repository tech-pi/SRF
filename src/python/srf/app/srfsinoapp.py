"""
Reconstruction with memory optimized:

sample code :: python

  import click
  import logging
  from dxl.learn.graph.reconstruction.reconstruction import main

  logger = logging.getLogger('dxl.learn.graph.reconstruction')
  logger.setLevel(logging.DEBUG)



  @click.command()
  @click.option('--job', '-j', help='Job')
  @click.option('--task', '-t', help='task', type=int, default=0)
  @click.option('--config', '-c', help='config file')
  def cli(job, task, config):
    main(job, task, config)

  if __name__ == "__main__":
    cli()

"""
import numpy as np
# import click
import tensorflow as tf

import pdb
import logging
import json
import h5py

# from ..task import TorTask
#from ..task import SRFTaskInfo, SinoTaskInfo
from ..specs.sinodata import SinoTaskSpec
#from ..task.tasksino_infonew import SinoTaskSpec
from dxl.learn.core import make_distribute_session
from ..preprocess.preprocess_sino import preprocess_sino
from dxl.learn.core.distribute import load_cluster_configs
from ..graph.pet.sino import SinoReconstructionTask



logging.basicConfig(
    level=logging.INFO,
    format='[%(levelname)s] %(asctime)s [%(filename)s:%(lineno)d] %(message)s',
    datefmt='%a, %d %b %Y %H:%M:%S',
)
logger = logging.getLogger('srfapp')


# TODO: Implement one unified config system for all applications.


def _load_config(path):
    """
    Helper function to load .json config file.

    Arguments:
    - `path` path to config file.

    Return:

    - dict of config

    Raises:
    - None

    """
    # TODO: rework to use dxl.fs
    from pathlib import Path
    import json
    with open(Path(path)) as fin:
        return json.load(fin)


def _load_config_if_not_dict(config):
    """
    Unified config_file(via path) and config dict.
    """
    if not isinstance(config, dict):
        return _load_config(config)
    return config


class SinoApp():
    """
    Scalable reconstruction framework high-level API. With following methods:
    """

    @classmethod
    def make_task(cls,
                  job,
                  task_index,
                  task_info,
                  distribution_config=None):
        return task_info.task_cls(job, task_index, task_info.info,
                                  distribution_config)

    @classmethod
    def reconstruction(cls, job, task_index, task_config, distribute_config):
        """
        Distribute reconstruction main entry. Call this function in different processes.
        """
        # task_config = _load_config_if_not_dict(task_config)
        # distribute_config = _load_config_if_not_dict(distribute_config)
        logging.info("Task config: {}.".format(task_config))
        logging.info("Distribute config: {}.".format(distribute_config))
        # task_info = TorTaskInfo(task_config)
        task_spec = SinoTaskSpec(task_config)
        if isinstance(distribute_config, str):
            with open(distribute_config, 'r') as fin:
                cluster_config = distribute_config
        else:
            cluster_config = dict(distribute_config)
        
        # task = task_spec.task_cls(
        # job, task_index, task_spec, distribute_config)
        # task.run()
        task = SinoReconstructionTask(
            task_spec, job=job, task_index=task_index, cluster_config=cluster_config)
        make_distribute_session()
        task.run_task()

    # @classmethod
    # def efficiency_map_single_ring(cls, job, task_index, task_config, distribute_config):
    #     pass

    # @classmethod
    # def efficiency_map_merge(cls, task_config):
    #     pass

    @classmethod
    def make_sino(cls, config):
        """
        Preprocessing data for TOR model based reconstruction.
        """
        ts = SinoTaskSpec(config)
        #preprocess_sino(ts)

    @classmethod
    def sino_auto_config(cls, recon_config, distribute_config, output=None):
        
        distribute_config = load_cluster_configs(distribute_config)
        nb_workers = distribute_config.get('nb_workers',
                                           len(distribute_config['worker']))
        ts = SinoTaskSpec(recon_config)
        #nb_subsets = ts.osem.nb_subsets
        
        with h5py.File(ts.sino.path_file, 'r') as fin:
            sino = fin[ts.sino.path_dataset]
