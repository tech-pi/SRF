#define GOOGLE_CUDA 1
#if GOOGLE_CUDA
#define EIGEN_USE_GPU
#include "third_party/eigen3/unsupported/Eigen/CXX11/Tensor"
#include "cuda.h"
#include "cuda_runtime.h"
#include <ctime>
///
/// calculate the cross point
const int MAX_ROW = 30;
const int MAX_SHARE = 200 * MAX_ROW;
const bool PROJECTION_USE_SHARED_MEMORY = false;
const bool BACK_PROJECTION_USE_SHARED_MEMORY = false;
const bool ATOMIC_ADD = true;
const bool SKIP_SMV = false;
const int GRIDDIM = 32;
const int BLOCKDIM = 1024;

const float TOF_THRESHOLD = 405758.0;

__device__ bool
CalculateCrossPoint(float pos1_x, float pos1_y, float pos1_z,
                    float pos2_x, float pos2_y, float pos2_z,
                    const float s_center,
                    float &dcos_x, float &dcos_y,
                    float &cross_x, float &cross_y)
{
    float d0 = pos2_x - pos1_x, d1 = pos2_y - pos1_y, d2 = pos2_z - pos1_z;
    float ratio = (s_center - pos1_z) / d2;
    bool flag = true;
    flag = flag && ratio > 0.0 && ratio < 1.0;
    // if (ratio < 0.0 || ratio > 1.0)
    // return false;
    //the x and y value of cross point.
    cross_x = pos1_x + d0 * ratio;
    cross_y = pos1_y + d1 * ratio;
    // flag = flag && cross_x < 100.0 && cross_x > -100.0 && cross_y < 100.0 && cross_y > -100.0;
    //the len of lor.
    float dis = std::sqrt(d0 * d0 + d1 * d1 + d2 * d2);
    //direction cosine of lor.
    dcos_x = d0 / dis;
    dcos_y = d1 / dis;
    //printf("calculate points: %f \n",s_center);
    return flag;
}

__device__ void CalculateSMV(const float xc, const float yc, const float zc, const float sigma2_factor,
                             const float slice_z, const float tof_bin, const float tof_sigma2,
                             const float cross_x, const float cross_y,
                             const float mesh_x, const float mesh_y,
                             const float dcos_x, const float dcos_y,
                             const float sigma2, float &value)
{
    if (SKIP_SMV)
    {
        value = 1.0;
        return;
    }
    float delta_x = cross_x - mesh_x;
    float delta_y = cross_y - mesh_y;
    float r_cos = (delta_x * dcos_x + delta_y * dcos_y);
    // the distance square betwwen mesh to the tube center line.
    float d2 = delta_x * delta_x + delta_y * delta_y - r_cos * r_cos;
    float sigma2_corrected = sigma2 * sigma2_factor;
    // float sigma2_corrected = sigma2 * 1.0;
    // value = (d2 < 9.0 * sigma2) ? std::exp(-0.5 * d2 / sigma2) : 0.0;
    // the distance square between mesh to the tof center.
    value = exp(-0.5 * d2 / sigma2_corrected);

    if (tof_sigma2 < TOF_THRESHOLD)
    { // when tof resolution is larger than 10000 ps, the TOF is disabled.
        float d2_tof = ((xc - cross_x) * (xc - cross_x) + (yc - cross_y) * (yc - cross_y) + (zc - slice_z) * (zc - slice_z) - d2);
        float tof_sigma2_expand = tof_sigma2 + (tof_bin * tof_bin) / 12;
        float t2 = d2_tof / tof_sigma2_expand;
        value *= tof_bin * exp(-0.5 * t2) / sqrt(2.0 * M_PI * sigma2);
    }
}

__device__ void MapSMV(const float sigma2_factor,
                       const float cross_x, const float cross_y,
                       const float mesh_x, const float mesh_y,
                       const float dcos_x, const float dcos_y,
                       const float sigma2, float &value)
{
    float delta_x = cross_x - mesh_x;
    float delta_y = cross_y - mesh_y;
    float r_cos = (delta_x * dcos_x + delta_y * dcos_y);
    float d2 = delta_x * delta_x + delta_y * delta_y - r_cos * r_cos;

    float sigma2_corrected = sigma2_factor * sigma2;
    value = (d2 < 9.0 * sigma2_corrected) ? std::exp(-0.5 * d2 / sigma2_corrected) : 0.0;
}

__device__ void LoopPatch(const float xc, const float yc, const float zc, const float sigma2_factor,
                          const float tof_bin, const float tof_sigma2, const float slice_z,
                          const unsigned patch_size, const unsigned int offset,
                          const float inter_x, const float inter_y,
                          const float cross_x, const float cross_y,
                          const float sigma2, const float dcos_x, const float dcos_y,
                          const float l_bound, const float b_bound, const int l0, const int l1,
                          float *projection_value, const float *image_data)
{
    // auto image_flat = image.flat<float>();
    // int l0 = image.dim_size(0);
    // int l1 = image.dim_size(1);
    //the start mesh of
    // return;
    int index_x = (int)((cross_x - l_bound) / inter_x) - (int)(patch_size / 2);
    int index_y = (int)((cross_y - b_bound) / inter_y) - (int)(patch_size / 2);
    for (int j = 0; j < patch_size; j++)
        for (int i = 0; i < patch_size; i++)
        {
            int index0 = index_x + i;
            int index1 = index_y + j;
            if (index0 < 0 || index0 >= l0 || index1 < 0 || index1 >= l1) // x axis index is out of slice range
                continue;
            int index = index0 + index1 * l0;
            float value = 0.0;
            // compute the system matrix value.
            CalculateSMV(xc, yc, zc, sigma2_factor, slice_z, tof_bin, tof_sigma2,
                         cross_x, cross_y,
                         inter_x * (index0 + 0.5) + l_bound,
                         inter_y * (index1 + 0.5) + b_bound,
                         dcos_x, dcos_y, sigma2, value);
            if (!ATOMIC_ADD)
                projection_value[0] += image_data[index + offset] * value;
            else
                atomicAdd(projection_value, image_data[index + offset] * value);
        }
}

// for backprojection
__device__ void BackLoopPatch(const float xc, const float yc, const float zc, const float sigma2_factor,
                              const float tof_bin, const float tof_sigma2, const float slice_z,
                              const unsigned patch_size, const unsigned int offset,
                              const float inter_x, const float inter_y,
                              const float cross_x, const float cross_y,
                              const float sigma2, const float dcos_x, const float dcos_y,
                              const float l_bound, const float b_bound, const int l0, const int l1,
                              const float projection_value, float *image_data)
{
    // int index_x = (int)((cross_x - l_bound) / inter_x) - (int)(patch_size / 2);
    // int index_y = (int)((cross_y - b_bound) / inter_y) - (int)(patch_size / 2);
    // return;
    int index_x = (int)((cross_x - l_bound) / inter_x) - (int)(patch_size / 2);
    int index_y = (int)((cross_y - b_bound) / inter_y) - (int)(patch_size / 2);
    for (int j = 0; j < patch_size; j++)
        for (int i = 0; i < patch_size; i++)
        {
            int index1 = index_y + j;
            int index0 = index_x + i;
            if (index0 < 0 || index0 >= l0 || index1 < 0 || index1 >= l1) // x axis index is out of slice range
                continue;
            int index = index0 + index1 * l0;
            float value = 0.0;
            // compute the system matrix value.
            CalculateSMV(xc, yc, zc, sigma2_factor, slice_z, tof_bin, tof_sigma2,
                         cross_x, cross_y,
                         inter_x * (index0 + 0.5) + l_bound,
                         inter_y * (index1 + 0.5) + b_bound,
                         //  inter_x * (index0 ) + l_bound,
                         //  inter_y * (index1 ) + b_bound,
                         dcos_x, dcos_y, sigma2, value);
            // return;
            if (projection_value > 0){
                if (!ATOMIC_ADD)
                    image_data[index] += value / projection_value;
                else
                    atomicAdd(image_data + offset + index, value / (projection_value));
            }

        }
}

// for mapping lors
__device__ void MapLoopPatch(const float sigma2_factor,
                             const unsigned patch_size, const unsigned int offset,
                             const float inter_x, const float inter_y,
                             const float cross_x, const float cross_y,
                             const float sigma2, const float dcos_x, const float dcos_y,
                             const float l_bound, const float b_bound, const int l0, const int l1,
                             const float projection_value, float *image_data)
{
    // int index_x = (int)((cross_x - l_bound) / inter_x) - (int)(patch_size / 2);
    // int index_y = (int)((cross_y - b_bound) / inter_y) - (int)(patch_size / 2);
    int index_x = (int)((cross_x - l_bound) / inter_x) - (int)(patch_size / 2);
    int index_y = (int)((cross_y - b_bound) / inter_y) - (int)(patch_size / 2);
    for (int j = 0; j < patch_size; j++)
    {
        int index1 = index_y + j;
        if (index1 < 0 || index1 >= l1) //y axis index is out of slice range
            continue;
        for (int i = 0; i < patch_size; i++)
        {
            int index0 = index_x + i;
            if (index0 < 0 || index0 >= l0) // x axis index is out of slice range
                continue;
            else
            {
                int index = index0 + index1 * l0;
                float value = 0.0;
                // compute the system matrix value.
                MapSMV(sigma2_factor,
                       cross_x, cross_y,
                       inter_x * (float)(0.5 + index0) + l_bound,
                       inter_y * (float)(0.5 + index1) + b_bound,
                       //  inter_x * (index0 ) + l_bound,
                       //  inter_y * (index1 ) + b_bound,
                       dcos_x, dcos_y, sigma2, value);
                if (projection_value > 0)
                    atomicAdd(image_data + offset + index, value / projection_value);
            }
        }
    }
}

__global__ void ComputeSlice(const float *x1, const float *y1, const float *z1,
                             const float *x2, const float *y2, const float *z2,
                             const float *xc, const float *yc, const float *zc,
                             const float *sigma2_factor,
                             const float tof_bin, const float tof_sigma2, const float slice_z,
                             const unsigned int patch_size, const unsigned int offset,
                             const float l_bound, const float b_bound, const float sigma2,
                             const int gx, const int gy, const float inter_x, const float inter_y,
                             float *projection_value, const int num_events, const float *image,
                             const int gz, const unsigned int slice_mesh_num, const float center_z, const float lz, const float inter_z)
{
    if (PROJECTION_USE_SHARED_MEMORY)
    {
        __shared__ float tile[MAX_SHARE];
        for (int gyl = 0; gyl < (gy + MAX_ROW); gyl += MAX_ROW)
        {
            for (int cid = threadIdx.x; cid < MAX_SHARE; cid += blockDim.x)
            {
                if (gyl * gx + cid > gx * gy)
                    continue;
                tile[cid] = image[offset + gyl * gx + cid];
            }
            int gy_now = min(MAX_ROW, gy - gyl);
            __syncthreads();
            for (int tid = blockIdx.x * blockDim.x + threadIdx.x; tid < num_events;
                 tid += blockDim.x * gridDim.x)
            {
                float dcos_x = 0;
                float dcos_y = 0;
                float cross_x = 0;
                float cross_y = 0;
                if (CalculateCrossPoint(x1[tid], y1[tid], z1[tid],
                                        x2[tid], y2[tid], z2[tid],
                                        slice_z, dcos_x, dcos_y, cross_x, cross_y))
                {

                    LoopPatch(xc[tid], yc[tid], zc[tid], sigma2_factor[tid],
                              tof_bin, tof_sigma2, slice_z,
                              patch_size, offset,
                              inter_x, inter_y, cross_x, cross_y,
                              sigma2, dcos_x, dcos_y,
                              l_bound, b_bound + gyl * inter_y, gx, gy_now,
                              projection_value + tid, tile);
                }
            }
            __syncthreads();
        }
    }
    else
    {
        // __shared__ float sx1[BLOCKDIM];
        // __shared__ float sy1[BLOCKDIM];
        // __shared__ float sz1[BLOCKDIM];
        // __shared__ float sx2[BLOCKDIM];
        // __shared__ float sy2[BLOCKDIM];
        // __shared__ float sz2[BLOCKDIM];
        // __shared__ float sxc[BLOCKDIM];
        // __shared__ float syc[BLOCKDIM];
        // __shared__ float szc[BLOCKDIM];
        int step = blockDim.x * gridDim.x;
        int jid = threadIdx.x;
        // printf("jid: %d\n", jid);
        for (int tid = blockIdx.x * blockDim.x + threadIdx.x; tid < (num_events + step);
             tid += step)
        {
            // if (tid < num_events)
            // {
            //     sx1[jid] = x1[tid];
            //     sx2[jid] = x2[tid];
            //     sxc[jid] = xc[tid];
            //     sy1[jid] = y1[tid];
            //     sy2[jid] = y2[tid];
            //     syc[jid] = yc[tid];
            //     sz1[jid] = z1[tid];
            //     sz2[jid] = z2[tid];
            //     szc[jid] = zc[tid];
            // }
            // __syncthreads();
            float dcos_x = 0;
            float dcos_y = 0;
            float cross_x = 0;
            float cross_y = 0;
            if (tid < num_events)
            {
                float z_start_f = min(z1[tid], z2[tid]);
                float z_end_f = max(z1[tid], z2[tid]);
                int z_start = int((z_start_f - center_z + lz / 2.0) / inter_z) - 1;
                int z_end = int((z_end_f - center_z + lz / 2.0) / inter_z) + 2;
                z_start = min(z_start, gz);
                z_end = min(z_end, gz);
                z_start = max(z_start, 0);
                z_end = max(z_end, 0);
                // z_start = 0;
                // z_end = gz;

                for (unsigned int iSlice = z_start; iSlice < z_end; iSlice++)
                {
                    int offset_new = iSlice * slice_mesh_num;
                    float slice_z_new = center_z - (lz - inter_z) / 2 + iSlice * inter_z;
                    // float cross_x, cross_y;
                    CalculateCrossPoint(x1[tid], y1[tid], z1[tid], x2[tid], y2[tid], z2[tid],
                                        slice_z_new, dcos_x, dcos_y, cross_x, cross_y);
                    LoopPatch(xc[tid], yc[tid], zc[tid], sigma2_factor[tid],
                              tof_bin, tof_sigma2, slice_z_new,
                              patch_size, offset_new,
                              inter_x, inter_y, cross_x, cross_y,
                              sigma2, dcos_x, dcos_y,
                              l_bound, b_bound, gx, gy,
                              projection_value + tid, image + offset_new);
                }
            }
            // __syncthreads();
        }
    }
}

///
///back
///
__global__ void BackComputeSlice(const float *x1, const float *y1, const float *z1,
                                 const float *x2, const float *y2, const float *z2,
                                 const float *xc, const float *yc, const float *zc,
                                 const float *sigma2_factor,
                                 const float tof_bin, const float tof_sigma2, const float slice_z,
                                 const unsigned int patch_size, const unsigned int offset,
                                 const float l_bound, const float b_bound, const float sigma2,
                                 const int gx, const int gy, const float inter_x, const float inter_y,
                                 const float *projection_value, const int num_events, float *image,
                                 const int gz, const unsigned int slice_mesh_num, const float center_z, const float lz, const float inter_z)
{

    if (BACK_PROJECTION_USE_SHARED_MEMORY)
    {
        __shared__ float tile[MAX_SHARE];
        for (int gyl = 0; gyl < (gy + MAX_ROW); gyl += MAX_ROW)
        {
            for (int cid = threadIdx.x; cid < MAX_SHARE; cid += blockDim.x)
            {
                if (gyl * gx + cid > gx * gy)
                    continue;
                tile[cid] = 0.0;
            }
            __syncthreads();
            int gy_now = min(MAX_ROW, gy - gyl);
            for (int tid = blockIdx.x * blockDim.x + threadIdx.x; tid < num_events;
                 tid += blockDim.x * gridDim.x)
            {
                float dcos_x = 0;
                float dcos_y = 0;
                float cross_x = 0;
                float cross_y = 0;
                if (CalculateCrossPoint(x1[tid], y1[tid], z1[tid],
                                        x2[tid], y2[tid], z2[tid],
                                        slice_z, dcos_x, dcos_y, cross_x, cross_y))
                {
                    BackLoopPatch(xc[tid], yc[tid], zc[tid], sigma2_factor[tid],
                                  tof_bin, tof_sigma2, slice_z,
                                  patch_size, offset,
                                  inter_x, inter_y, cross_x, cross_y,
                                  sigma2, dcos_x, dcos_y,
                                  l_bound, b_bound + gyl * inter_y, gx, gy_now,
                                  projection_value[tid], tile);
                }
            }
            __syncthreads();
            for (int cid = threadIdx.x; cid < MAX_SHARE; cid += blockDim.x)
            {
                if (gyl * gx + cid > gx * gy)
                    continue;
                atomicAdd(image + offset + gyl * gx + cid, tile[cid]);
            }
            __syncthreads();
        }
    }
    else
    {
        for (int tid = blockIdx.x * blockDim.x + threadIdx.x; tid < num_events;
             tid += blockDim.x * gridDim.x)
        {
            float dcos_x = 0;
            float dcos_y = 0;
            float cross_x = 0;
            float cross_y = 0;
            if (tid < num_events)
            {
                float z_start_f = min(z2[tid], z1[tid]);
                float z_end_f = max(z2[tid], z1[tid]);
                int z_start = int((z_start_f - center_z + lz / 2.0) / inter_z) - 1;
                int z_end = int((z_end_f - center_z + lz / 2.0) / inter_z) + 2;
                z_start = min(z_start, gz);
                z_end = min(z_end, gz);
                z_start = max(z_start, 0);
                z_end = max(z_end, 0);

                // z_start = 0;
                // z_end = gz;
                for (unsigned int iSlice = z_start; iSlice < z_end; iSlice++)
                {
                    int offset_new = iSlice * slice_mesh_num;
                    float slice_z_new = center_z - (lz - inter_z) / 2 + iSlice * inter_z;
                    CalculateCrossPoint(x1[tid], y1[tid], z1[tid],
                                        x2[tid], y2[tid], z2[tid],
                                        slice_z_new, dcos_x, dcos_y, cross_x, cross_y);
                    BackLoopPatch(xc[tid], yc[tid], zc[tid], sigma2_factor[tid],
                                  tof_bin, tof_sigma2, slice_z_new,
                                  patch_size, offset_new,
                                  inter_x, inter_y, cross_x, cross_y,
                                  sigma2, dcos_x, dcos_y,
                                  l_bound, b_bound, gx, gy,
                                  projection_value[tid], image + offset_new);
                }
            }
        }
    }
}

__global__ void Mapping(const float *x1, const float *y1, const float *z1,
                        const float *x2, const float *y2, const float *z2,
                        const float *sigma2_factor,
                        const unsigned int patch_size, const unsigned int offset,
                        const float slice_z,
                        const float l_bound, const float b_bound, const float sigma2,
                        const int gx, const int gy, const float inter_x, const float inter_y,
                        const float *projection_value, const int num_events, float *image)
{

    for (int tid = blockIdx.x * blockDim.x + threadIdx.x; tid < num_events;
         tid += blockDim.x * gridDim.x)
    {
        float dcos_x = 0;
        float dcos_y = 0;
        float cross_x = 0;
        float cross_y = 0;
        if (CalculateCrossPoint(x1[tid], y1[tid], z1[tid],
                                x2[tid], y2[tid], z2[tid],
                                slice_z, dcos_x, dcos_y, cross_x, cross_y))
        {
            MapLoopPatch(sigma2_factor[tid],
                         patch_size, offset,
                         inter_x, inter_y, cross_x, cross_y,
                         sigma2, dcos_x, dcos_y,
                         l_bound, b_bound, gx, gy,
                         projection_value[tid], image);
        }
    }
}

void projection(const float *x1, const float *y1, const float *z1,
                const float *x2, const float *y2, const float *z2,
                const float *xc, const float *yc, const float *zc,
                const float *sigma2_factor,
                float *projection_value,
                const int *grid, const float *center, const float *size,
                const float kernel_width,
                const float tof_bin, const float tof_sigma2,
                const float *image, const int num_events)
{
    // auto grid_flat = grid.flat<int>();
    // auto center_flat = center.flat<float>();
    // auto size_flat = size.flat<float>();
    // auto projection_value_flat = projection_value->flat<float>();
    // int num_events = projection_value->size();
    // std::cout << "here we stop0!" << std::endl;
    // std::cout << grid << std::endl;
    int grid_cpu[3];
    float center_cpu[3];
    float size_cpu[3];
    cudaMemcpy(grid_cpu, grid, 3 * sizeof(int), cudaMemcpyDeviceToHost);
    cudaMemcpy(center_cpu, center, 3 * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(size_cpu, size, 3 * sizeof(float), cudaMemcpyDeviceToHost);
    unsigned int gx = grid_cpu[0], gy = grid_cpu[1], gz = grid_cpu[2]; //number of meshes
    // std::cout << gx << " " << gy << " " << gz << std::endl;
    float center_x = center_cpu[0], center_y = center_cpu[1], center_z = center_cpu[2]; // position of center
    float lx = size_cpu[0], ly = size_cpu[1], lz = size_cpu[2];                         // length of bounds
    unsigned int slice_mesh_num = gx * gy;                                              // number of meshes in a slice.

    float inter_x = lx / gx, inter_y = ly / gy, inter_z = lz / gz; // intervals

    float l_bound = center_x - lx / 2, b_bound = center_y - ly / 2; // left and bottom bound of the slice.
    //float kernel_width = 3;                                         //this->app->get_kernel_width();
    // int patch_size = (kernel_width * 2 * std::sqrt(2) + (lz / gz)) / (lx / gx) + 1;

    //sigma2 indicate the bound of a gaussian kernel with the relationship: 3*sigma = kernel_width.
    float sigma2 = kernel_width * kernel_width / 36;
    int patch_size = std::ceil((std::sqrt(2) * kernel_width + lz / gz) / (lx / gx));
    // int patch_size = (kernel_width * 2 * std::sqrt(2) + (lz / gz)) / (lx / gx) + 1;
    // float dcos_x, dcos_y;
    // bool measure_time = false;
    // float t;
    // t = clock();
    // for (unsigned int iSlice = 0; iSlice < gz; iSlice++)
    // {
    //     int offset = iSlice * slice_mesh_num;
    //     float slice_z = center_z - (lz - inter_z) / 2 + iSlice * inter_z;
    //     // float cross_x, cross_y;
    int offset = 0;
    float slice_z = 0.0;
    // std::cout << "Compute Clice Called.";
    ComputeSlice<<<GRIDDIM, BLOCKDIM>>>(x1, y1, z1, x2, y2, z2, xc, yc, zc, sigma2_factor,
                                        tof_bin, tof_sigma2, slice_z,
                                        patch_size, offset,
                                        l_bound, b_bound, sigma2,
                                        gx, gy, inter_x, inter_y,
                                        projection_value, num_events,
                                        image,
                                        gz, slice_mesh_num, center_z, lz, inter_z);
    // }
    // cudaDeviceSynchronize();
    // std::cout << "Projection time cost:" << clock() - t << std::endl;
}

void backprojection(const float *x1, const float *y1, const float *z1,
                    const float *x2, const float *y2, const float *z2,
                    const float *xc, const float *yc, const float *zc,
                    const float *sigma2_factor,
                    const float *projection_value,
                    const int *grid, const float *center, const float *size,
                    const float kernel_width,
                    const float tof_bin, const float tof_sigma2,
                    float *image, const int num_events)
{
    // auto grid_flat = grid.flat<int>();
    // auto center_flat = center.flat<float>();
    // auto size_flat = size.flat<float>();
    // auto projection_value_flat = projection_value->flat<float>();
    // int num_events = projection_value->size();
    // std::cout << "here we stop0!" << std::endl;
    // std::cout << grid << std::endl;
    int grid_cpu[3];
    float center_cpu[3];
    float size_cpu[3];
    cudaMemcpy(grid_cpu, grid, 3 * sizeof(int), cudaMemcpyDeviceToHost);
    cudaMemcpy(center_cpu, center, 3 * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(size_cpu, size, 3 * sizeof(float), cudaMemcpyDeviceToHost);
    unsigned int gx = grid_cpu[0], gy = grid_cpu[1], gz = grid_cpu[2]; //number of meshes
    // std::cout << gx << " " << gy << " " << gz << std::endl;
    float center_x = center_cpu[0], center_y = center_cpu[1], center_z = center_cpu[2]; // position of center
    float lx = size_cpu[0], ly = size_cpu[1], lz = size_cpu[2];                         // length of bounds
    unsigned int slice_mesh_num = gx * gy;                                              // number of meshes in a slice.

    // float inter_x = lx / gx, inter_y = lx / gy, inter_z = lz / gz;  // intervals
    float inter_x = lx / gx, inter_y = ly / gy, inter_z = lz / gz; // intervals
    // std::cout << "Pixel Size: " << inter_x << " " << inter_y << " " << inter_z <<std::endl;
    float l_bound = center_x - lx / 2, b_bound = center_y - ly / 2; // left and bottom bound of the slice.
    // int patch_size = (kernel_width * 2 * std::sqrt(2) + (lz / gz)) / (lx / gx) + 10;
    float sigma2 = kernel_width * kernel_width / 36;
    // int patch_size = (kernel_width * 2 * std::sqrt(2) + (lz / gz)) / (lx / gx) + 1;
    int patch_size = std::ceil((std::sqrt(2) * kernel_width + lz / gz) / (lx / gx));
    // float dcos_x, dcos_y;
    // std::cout<<"number of events:!!!!!!"<<num_events<<std::endl;
    // float t;
    // t = clock();
    // for (unsigned int iSlice = 0; iSlice < gz; iSlice++)
    // {
    // int offset = iSlice * slice_mesh_num;
    // int slice_z = center_z - (lz - inter_z) / 2 + iSlice * inter_z;
    // float slice_z = center_z - (lz - inter_z) / 2.0 + iSlice * inter_z;
    // std :: cout << "slice_z" << slice_z << std::endl;
    // float cross_x, cross_y;
    int offset = 0;
    float slice_z = 0.0;
    BackComputeSlice<<<GRIDDIM, BLOCKDIM>>>(x1, y1, z1, x2, y2, z2, xc, yc, zc, sigma2_factor,
                                            tof_bin, tof_sigma2, slice_z,
                                            patch_size, offset,
                                            l_bound, b_bound, sigma2,
                                            gx, gy, inter_x, inter_y,
                                            projection_value, num_events,
                                            image,
                                            gz, slice_mesh_num, center_z, lz, inter_z);
    // }
    // cudaDeviceSynchronize();
    // std::cout << "BackProjection time cost:" << clock() - t << std::endl;
}

void maplors(const float *x1, const float *y1, const float *z1,
             const float *x2, const float *y2, const float *z2,
             const float *sigma2_factor,
             const float *projection_value,
             const int *grid, const float *center, const float *size,
             const float kernel_width,
             float *image, const int num_events)
{
    // auto grid_flat = grid.flat<int>();
    // auto center_flat = center.flat<float>();
    // auto size_flat = size.flat<float>();
    // auto projection_value_flat = projection_value->flat<float>();
    // int num_events = projection_value->size();
    // std::cout << "here we stop0!" << std::endl;
    // std::cout << grid << std::endl;
    int grid_cpu[3];
    float center_cpu[3];
    float size_cpu[3];
    cudaMemcpy(grid_cpu, grid, 3 * sizeof(int), cudaMemcpyDeviceToHost);
    cudaMemcpy(center_cpu, center, 3 * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(size_cpu, size, 3 * sizeof(float), cudaMemcpyDeviceToHost);
    unsigned int gx = grid_cpu[0], gy = grid_cpu[1], gz = grid_cpu[2]; //number of meshes
    // std::cout << gx << " " << gy << " " << gz << std::endl;
    float center_x = center_cpu[0], center_y = center_cpu[1], center_z = center_cpu[2]; // position of center
    float lx = size_cpu[0], ly = size_cpu[1], lz = size_cpu[2];                         // length of bounds
    // std::cout << "image Size: " << lx << " " << ly << " " << lz <<std::endl;
    unsigned int slice_mesh_num = gx * gy;                                              // number of meshes in a slice.

    float inter_x = lx / gx, inter_y = ly / gy, inter_z = lz / gz;  // intervals
    float l_bound = center_x - lx / 2, b_bound = center_y - ly / 2; // left and bottom bound of the slice.

    //sigma2 indicate the bound of a gaussian kernel with the relationship: 3*sigma = kernel_width.
    float sigma2 = kernel_width * kernel_width / 36;
    int patch_size = std::ceil((std::sqrt(2) * kernel_width + lz / gz) / (lx / gx));
    // int patch_size = (kernel_width * 2 * std::sqrt(2) + (lz / gz)) / (lx / gx) + 1;

    std::cout << "number of events: " << num_events << std::endl;

    for (unsigned int iSlice = 0; iSlice < gz; iSlice++)
    {
        int offset = iSlice * slice_mesh_num;
        float slice_z = center_z - (lz - inter_z) / 2.0 + iSlice * inter_z;
        // std::cout<<"slice_z:" << slice_z<<std::endl;
        Mapping<<<32, 1024>>>(x1, y1, z1, x2, y2, z2, sigma2_factor,
                              patch_size, offset, slice_z,
                              l_bound, b_bound, sigma2,
                              gx, gy, inter_x, inter_y,
                              projection_value, num_events,
                              image);
    }
}

#endif
