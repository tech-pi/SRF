import numpy as np
import time

def merge_effmap(start, end, num_rings, file_dir):
  """
  to do: implemented in GPU to reduce the calculated time
  """
  root = '/home/chengaoyu/code/Python/gitRepository/dxlearn/develop-cgy/data/'
  temp = np.load(root+file_dir+'effmap_{}.npy'.format(0))
  # print(temp.shape)
  num_image_layers = int(temp.shape[0])
  final_map = np.zeros(temp.shape)
  print(final_map.shape)
  st = time.time()
  # num_rings = end - start
  for ir in range(start, end):
    temp = np.load(root+file_dir+'effmap_{}.npy'.format(ir))
    print("process :{}/{}".format(ir+1, num_rings))
    for jr in range(num_rings - ir):
      if ir == 0:
        final_map[jr:num_image_layers,:,:] += temp[0:num_image_layers-jr,:,:]/2
      else:
        final_map[jr:num_image_layers,:,:] += temp[0:num_image_layers-jr,:,:]
    et = time.time()
    tr = (et -st)/(num_rings*(num_rings-1)/2 - (num_rings - ir - 1)*(num_rings-ir-2)/2)*((num_rings - ir-1)*(num_rings-ir-2)/2)
    print("time used: {} seconds".format(et-st))
    print("estimated time remains: {} seconds".format(tr))
  # odd = np.arange(0, num_rings, 2)
  # even = np.arange(1, num_rings, 2)
  # final_map = final_map[:,:,odd] + final_map[:,:, even] 
  
  # normalize the max value of the map to 1.
  cut_start = int((num_image_layers-num_rings)/2)
  # print(cut_start)
  final_map = final_map[cut_start:num_image_layers-cut_start,:,:]
  final_map = final_map/np.max(final_map)
  np.save(root+file_dir+'summap_{}to{}.npy'.format(start, end-1), final_map)

if __name__ == '__main__':
    merge_effmap(0, 100, 100, 'mono_maps/')