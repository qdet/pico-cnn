/** 
 * @brief AlexNet implementation as provided in the BVLC model zoo:
 * https://github.com/BVLC/caffe/tree/master/models/bvlc_alexnet
 *
 * @author Konstantin Luebeck (University of Tuebingen, Chair for Embedded Systems)
 */

#define JPEG
#define IMAGENET
//#define DEBUG
#define ARM_NEON

#include "pico-cnn/pico-cnn.h"
#include <omp.h>

void usage() {
    printf("./ekut_es_alexnet_arm_cpu \\\n"); 
    printf("PATH_TO_EKUT_ES_ALEXNET_WEIGHTS.weights \\\n");
    printf("PATH_TO_MEANS_FILE.means \\\n");
    printf("PATH_TO_IMAGE_NET_LABELS.txt \\\n");
    printf("PATH_TO_IMAGE.jpg\n");

}

/**
 * @brief sorts the prediction and the labels (in place) of the network such that the label with the
 * highes prediction is at the front of the array (position 0)
 *
 * @param prediction (1 x length)
 * @param labels (1 x length)
 * @param length
 */

void sort_prediction(fp_t* prediction, uint16_t* labels_pos, const uint16_t length) {
    // simple bubble sort
    uint16_t i,j;

    fp_t temp_prediction;
    uint16_t temp_label;

    for(i = 0; i < length-1; i++) {
        for(j = 0; j < length-1-i; j++) {
            if(prediction[j] < prediction[j+1]) {
                // swap
                temp_prediction = prediction[j];
                prediction[j] = prediction[j+1];
                prediction[j+1] = temp_prediction;

                temp_label = labels_pos[j];
                labels_pos[j] = labels_pos[j+1];
                labels_pos[j+1] = temp_label;
            }
        }

    }

}


int main(int argc, char** argv) {

    if(argc != 5) {
        fprintf(stderr, "too few or to many arguments!\n");
        usage();
        return 1;

    }

    char weights_path[1024];
    char means_path[1024];
    char labels_path[1024];
    char jpeg_path[1024];

    strcpy(weights_path, argv[1]);
    strcpy(means_path, argv[2]);
    strcpy(labels_path, argv[3]);
    strcpy(jpeg_path, argv[4]);

    unsigned int i, j;

    // read kernels and biasses
    fp_t*** kernels;
    fp_t** biasses;

    printf("reading weights from '%s'\n", weights_path);

    if(read_weights(weights_path, &kernels, &biasses) != 0) {
        fprintf(stderr, "could not read weights from '%s'\n", weights_path);
        return 1;

    }


    // read means
    printf("reading means from '%s'\n", means_path);

    fp_t* means = (fp_t*) malloc(3*sizeof(fp_t));

    if(read_means(means_path, means) != 0) {
        fprintf(stderr, "could not read means file '%s'!\n", means_path);
        return 1;
    }


    // read labels
    printf("reading labels from '%s'\n", labels_path);

    char** labels;
    int num_labels;
    num_labels = read_imagenet_labels(labels_path, &labels, 1000);

    if(num_labels != 1000) {
        fprintf(stderr, "could not read imagenet labels '%s'\n", labels_path);
        return 1;
    }


    // read input image
    printf("reading input image '%s'\n", jpeg_path);

    fp_t** pre_mean_input;

    uint16_t height;
    uint16_t width;

    read_jpeg(&pre_mean_input, jpeg_path, 0.0, 255.0, &height, &width);

    // make pgm of pre mean input image
    #ifdef DEBUG
    float* pre_mean_input_file_content = (fp_t*) malloc(227*227*3*sizeof(fp_t));
    for(j = 0; j < 3; j++) {
        memcpy(&pre_mean_input_file_content[j*227*227], pre_mean_input[j], 227*227*sizeof(fp_t));
    }
    
    write_pgm(pre_mean_input_file_content, 3*227, 227, "pre_mean_input.pgm");
    write_float(pre_mean_input_file_content, 3*227, 227, "pre_mean_input.float");
    free(pre_mean_input_file_content);
    #endif

    // substract mean from each channel
    fp_t** input = (fp_t**) malloc(3*sizeof(fp_t*));
    input[0] = (fp_t*) malloc(227*227*sizeof(fp_t));
    input[1] = (fp_t*) malloc(227*227*sizeof(fp_t));
    input[2] = (fp_t*) malloc(227*227*sizeof(fp_t));
        
    uint16_t row;
    uint16_t column;

    for(i = 0; i < 3; i++) {
        for(row = 0; row < height; row++) {
            for(column = 0; column < height; column++) {
                input[i][row*width+column] = pre_mean_input[i][row*width+column] - means[i];
            }
        }
    }

    // free pre mean input image
    for(i = 0; i < 3; i++) {
        free(pre_mean_input[i]);
    }
    free(pre_mean_input);

    // free means
    free(means);
    
    // make pgm of input image
    #ifdef DEBUG
    float* input_file_content = (fp_t*) malloc(227*227*3*sizeof(fp_t));
    for(j = 0; j < 3; j++) {
        memcpy(&input_file_content[j*227*227], input[j], 227*227*sizeof(fp_t));
    }
    
    write_pgm(input_file_content, 3*227, 227, "input.pgm");
    write_float(input_file_content, 3*227, 227, "input.float");
    free(input_file_content);
    #endif


    // conv1
    fp_t** conv1_output;
    conv1_output = (fp_t**) malloc(96*sizeof(fp_t*));

    for(i = 0; i < 96; i++) {
        conv1_output[i] = (fp_t*) malloc(55*55*sizeof(fp_t));
    }

    fp_t** conv1_intermediate = (fp_t**) malloc(omp_get_max_threads()*sizeof(fp_t*));

    for(i = 0; i < omp_get_max_threads(); i++) {
        conv1_intermediate[i] = (fp_t*) malloc(55*55*sizeof(fp_t));
    }

    fp_t** conv1_kernels = kernels[0];
    fp_t* conv1_bias = biasses[0];

    // norm1
    fp_t** norm1_output;
    norm1_output = (fp_t**) malloc(96*sizeof(fp_t*));
    
    for(i = 0; i < 96; i++) {
        norm1_output[i] = (fp_t*) malloc(55*55*sizeof(fp_t));
    }

    // pool1
    fp_t** pool1_output;
    pool1_output = (fp_t**) malloc(96*sizeof(fp_t*));
    
    for(i = 0; i < 96; i++) {
        pool1_output[i] = (fp_t*) malloc(27*27*sizeof(fp_t));
    }

    // conv2
    fp_t** conv2_output;
    conv2_output = (fp_t**) malloc(256*sizeof(fp_t*));

    for(i = 0; i < 256; i++) {
        conv2_output[i] = (fp_t*) malloc(27*27*sizeof(fp_t));
    }

    fp_t** conv2_intermediate = (fp_t**) malloc(omp_get_max_threads()*sizeof(fp_t*));

    for(i = 0; i < omp_get_max_threads(); i++) {
        conv2_intermediate[i] = (fp_t*) malloc(27*27*sizeof(fp_t));
    }

    fp_t** conv2_kernels = kernels[1];
    fp_t* conv2_bias = biasses[1];

    // norm2
    fp_t** norm2_output;
    norm2_output = (fp_t**) malloc(256*sizeof(fp_t*));
    
    for(i = 0; i < 256; i++) {
        norm2_output[i] = (fp_t*) malloc(27*27*sizeof(fp_t));
    }

    // pool2
    fp_t** pool2_output;
    pool2_output = (fp_t**) malloc(256*sizeof(fp_t*));
    
    for(i = 0; i < 256; i++) {
        pool2_output[i] = (fp_t*) malloc(13*13*sizeof(fp_t));
    } 

    // conv3
    fp_t** conv3_output;
    conv3_output = (fp_t**) malloc(384*sizeof(fp_t*));

    for(i = 0; i < 384; i++) {
        conv3_output[i] = (fp_t*) malloc(13*13*sizeof(fp_t));
    }

    fp_t** conv3_intermediate = (fp_t**) malloc(omp_get_max_threads()*sizeof(fp_t*));

    for(i = 0; i < omp_get_max_threads(); i++) {
        conv3_intermediate[i] = (fp_t*) malloc(13*13*sizeof(fp_t));
    }

    fp_t** conv3_kernels = kernels[2];
    fp_t* conv3_bias = biasses[2];

    // conv4
    fp_t** conv4_output;
    conv4_output = (fp_t**) malloc(384*sizeof(fp_t*));

    for(i = 0; i < 384; i++) {
        conv4_output[i] = (fp_t*) malloc(13*13*sizeof(fp_t));
    }

    fp_t** conv4_intermediate = (fp_t**) malloc(omp_get_max_threads()*sizeof(fp_t*));
   
    for(i = 0; i < omp_get_max_threads(); i++) {
        conv4_intermediate[i] = (fp_t*) malloc(13*13*sizeof(fp_t));
    }

    fp_t** conv4_kernels = kernels[3];
    fp_t* conv4_bias = biasses[3];

    // conv5
    fp_t** conv5_output;
    conv5_output = (fp_t**) malloc(256*sizeof(fp_t*));

    for(i = 0; i < 256; i++) {
        conv5_output[i] = (fp_t*) malloc(13*13*sizeof(fp_t));
    }

    fp_t** conv5_intermediate = (fp_t**) malloc(omp_get_max_threads()*sizeof(fp_t*));

    for(i = 0; i < omp_get_max_threads(); i++) {
        conv5_intermediate[i] = (fp_t*) malloc(13*13*sizeof(fp_t));
    }

    fp_t** conv5_kernels = kernels[4];
    fp_t* conv5_bias = biasses[4];

    // pool5
    fp_t** pool5_output;
    pool5_output = (fp_t**) malloc(256*sizeof(fp_t*));
    
    for(i = 0; i < 256; i++) {
        pool5_output[i] = (fp_t*) malloc(6*6*sizeof(fp_t));
    }

    // fc6
    fp_t* pool5_output_merged = (fp_t*) malloc(6*6*256*sizeof(fp_t));
    fp_t* fc6_output;
    fc6_output = (fp_t*) malloc(4096*sizeof(fp_t));

	restructure_fully_connected_kernel(&kernels[5][0], 9216, 4096);

    fp_t* fc6_kernel = kernels[5][0];
    fp_t* fc6_bias = biasses[5];

    // fc7
    fp_t* fc7_output;
    fc7_output = (fp_t*) malloc(4096*sizeof(fp_t));

	restructure_fully_connected_kernel(&kernels[6][0], 4096, 4096);

    fp_t* fc7_kernel = kernels[6][0];
    fp_t* fc7_bias = biasses[6];

    // fc8
    fp_t* fc8_output;
    fc8_output = (fp_t*) malloc(1000*sizeof(fp_t));

	restructure_fully_connected_kernel(&kernels[7][0], 4096, 1000);

    fp_t* fc8_kernel = kernels[7][0];
    fp_t* fc8_bias = biasses[7];


    printf("starting CNN\n");

    // conv1 input 227x227x3 -> output 55x55x96
    uint16_t kernel_number = 0;

    #pragma omp parallel for private(i)
    for(i = 0; i < 96; i++) {
        convolution2d_cpu_11x11_s4_valid(input[0], 227, 227, conv1_output[i], conv1_kernels[i*3+0], 0.0);

        convolution2d_cpu_11x11_s4_valid(input[1], 227, 227, conv1_intermediate[omp_get_thread_num()], conv1_kernels[i*3+1], 0.0);
        add_channel2d_cpu(conv1_output[i], conv1_intermediate[omp_get_thread_num()], 55, 55);

        convolution2d_cpu_11x11_s4_valid(input[2], 227, 227, conv1_intermediate[omp_get_thread_num()], conv1_kernels[i*3+2], conv1_bias[i]);
        add_channel2d_cpu(conv1_output[i], conv1_intermediate[omp_get_thread_num()], 55, 55);
    }

    // make pgm of input image
    #ifdef DEBUG
    fp_t* conv1_file_content = (fp_t*) malloc(55*55*96*sizeof(fp_t));
    for(i = 0; i < 96; i++) {
        memcpy(&conv1_file_content[i*55*55], conv1_output[i], 55*55*sizeof(fp_t));
    }
    
    write_pgm(conv1_file_content, 96*55, 55, "conv1_output.pgm");
    write_float(conv1_file_content, 96*55, 55, "conv1_output.float");
    free(conv1_file_content);
    #endif

   
    // relu1
    #pragma omp parallel for private(i)
    for(i = 0; i < 96; i++) {
        relu_cpu(conv1_output[i], 55, 55, conv1_output[i]);
    }

    // make pgm of relu1 output
    #ifdef DEBUG
    fp_t* relu1_file_content = (fp_t*) malloc(55*55*96*sizeof(fp_t));
    for(i = 0; i < 96; i++) {
        memcpy(&relu1_file_content[i*55*55], conv1_output[i], 55*55*sizeof(fp_t));
    }
    write_pgm(relu1_file_content, 96*55, 55, "relu1_output.pgm");
    write_float(relu1_file_content, 96*55, 55, "relu1_output.float");
    free(relu1_file_content);
    #endif


    // norm1
    local_response_normalization_cpu_single(conv1_output, 55, 55, 96, norm1_output, 0.0001, 0.75, 5);

    // make pgm of norm1 output
    #ifdef DEBUG
    fp_t* norm1_file_content = (fp_t*) malloc(55*55*96*sizeof(fp_t));
    for(i = 0; i < 96; i++) {
        memcpy(&norm1_file_content[i*55*55], norm1_output[i], 55*55*sizeof(fp_t));
    }
    write_pgm(norm1_file_content, 96*55, 55, "norm1_output.pgm");
    write_float(norm1_file_content, 96*55, 55, "norm1_output.float");
    free(norm1_file_content);
    #endif

    
    // pool1 55x55x96 -> 27x27x96
    #pragma omp parallel for private(i)
    for(i = 0; i < 96; i++) {
        max_pooling2d_cpu_3x3_s2(norm1_output[i], 55, 55, pool1_output[i]);
    }

    // make pgm of pool1 output
    #ifdef DEBUG
    fp_t* pool1_file_content = (fp_t*) malloc(27*27*96*sizeof(fp_t));
    for(i = 0; i < 96; i++) {
        memcpy(&pool1_file_content[i*27*27], pool1_output[i], 27*27*sizeof(fp_t));
    }
    write_pgm(pool1_file_content, 96*27, 27, "pool1_output.pgm");
    write_float(pool1_file_content, 96*27, 27, "pool1_output.float");
    free(pool1_file_content);
    #endif


    // conv2 27x27x96 -> 27x27x256 
    #pragma omp parallel for private(i,j)
    for(i = 0; i < 256; i++) {
        convolution2d_cpu_5x5_s1_same(pool1_output[0], 27, 27, conv2_output[i], conv2_kernels[i*96], 0.0);

        for(j = 1; j < 95; j++) {
            convolution2d_cpu_5x5_s1_same(pool1_output[j], 27, 27, conv2_intermediate[omp_get_thread_num()], conv2_kernels[i*96+j], 0.0);
            add_channel2d_cpu(conv2_output[i], conv2_intermediate[omp_get_thread_num()], 27, 27);
        }
        convolution2d_cpu_5x5_s1_same(pool1_output[95], 27, 27, conv2_intermediate[omp_get_thread_num()], conv2_kernels[i*96+95], conv2_bias[i]);
        add_channel2d_cpu(conv2_output[i], conv2_intermediate[omp_get_thread_num()], 27, 27);
    }
    
    // make pgm of conv2 output
    #ifdef DEBUG
    fp_t* conv2_file_content = (fp_t*) malloc(27*27*256*sizeof(fp_t));
    for(i = 0; i < 256; i++) {
        memcpy(&conv2_file_content[i*27*27], conv2_output[i], 27*27*sizeof(fp_t));
    }
    write_pgm(conv2_file_content, 256*27, 27, "conv2_output.pgm");
    write_float(conv2_file_content, 256*27, 27, "conv2_output.float");
    free(conv2_file_content);
    #endif
 

    // relu2
    #pragma omp parallel for private(i)
    for(i = 0; i < 256; i++) {
        relu_cpu(conv2_output[i], 27, 27, conv2_output[i]);
    }

    // make pgm of relu1 output
    #ifdef DEBUG
    fp_t* relu2_file_content = (fp_t*) malloc(27*27*256*sizeof(fp_t));
    for(i = 0; i < 256; i++) {
        memcpy(&relu2_file_content[i*27*27], conv2_output[i], 27*27*sizeof(fp_t));
    }
    write_pgm(relu2_file_content, 256*27, 27, "relu2_output.pgm");
    write_float(relu2_file_content, 256*27, 27, "relu2_output.float");
    free(relu2_file_content);
    #endif


    // norm2
    local_response_normalization_cpu_single(conv2_output, 27, 27, 256, norm2_output, 0.0001, 0.75, 5);

    // make pgm of norm2 output
    #ifdef DEBUG
    fp_t* norm2_file_content = (fp_t*) malloc(27*27*256*sizeof(fp_t));
    for(i = 0; i < 256; i++) {
        memcpy(&norm2_file_content[i*27*27], norm2_output[i], 27*27*sizeof(fp_t));
    }
    write_pgm(norm2_file_content, 256*27, 27, "norm2_output.pgm");
    write_float(norm2_file_content, 256*27, 27, "norm2_output.float");
    free(norm2_file_content);
    #endif


    // pool2 27x27x256 -> 13x13x256
    #pragma omp parallel for private(i)
    for(i = 0; i < 256; i++) {
        max_pooling2d_cpu_3x3_s2(norm2_output[i], 27, 27, pool2_output[i]);
    }

    // make pgm of pool2 output
    #ifdef DEBUG
    fp_t* pool2_file_content = (fp_t*) malloc(13*13*256*sizeof(fp_t));
    for(i = 0; i < 256; i++) {
        memcpy(&pool2_file_content[i*13*13], pool2_output[i], 13*13*sizeof(fp_t));
    }
    write_pgm(pool2_file_content, 256*13, 13, "pool2_output.pgm");
    write_float(pool2_file_content, 256*13, 13, "pool2_output.float");
    free(pool2_file_content);
    #endif

    #pragma omp parallel for private(i,j) 
    // conv3 13x13x256 -> 13x13x384
    for(i = 0; i < 384; i++) {
        convolution2d_cpu_3x3_s1_same(pool2_output[0], 13, 13, conv3_output[i], conv3_kernels[i*256], 0.0);

        for(j = 1; j < 255; j++) {
            convolution2d_cpu_3x3_s1_same(pool2_output[j], 13, 13, conv3_intermediate[omp_get_thread_num()], conv3_kernels[i*256+j], 0.0);
            add_channel2d_cpu(conv3_output[i], conv3_intermediate[omp_get_thread_num()], 13, 13);
        }
        convolution2d_cpu_3x3_s1_same(pool2_output[255], 13, 13, conv3_intermediate[omp_get_thread_num()], conv3_kernels[i*256+255], conv3_bias[i]);
        add_channel2d_cpu(conv3_output[i], conv3_intermediate[omp_get_thread_num()], 13, 13);
    }

    // make pgm of conv2 output
    #ifdef DEBUG
    fp_t* conv3_file_content = (fp_t*) malloc(13*13*384*sizeof(fp_t));
    for(i = 0; i < 384; i++) {
        memcpy(&conv3_file_content[i*13*13], conv3_output[i], 13*13*sizeof(fp_t));
    }
    write_pgm(conv3_file_content, 256*13, 13, "conv3_output.pgm");
    write_float(conv3_file_content, 256*13, 13, "conv3_output.float");
    free(conv3_file_content);
    #endif

    
    // relu3
    #pragma omp parallel for private(i)
    for(i = 0; i < 384; i++) {
        relu_cpu(conv3_output[i], 13, 13, conv3_output[i]);
    }

    // make pgm of relu1 output
    #ifdef DEBUG
    fp_t* relu3_file_content = (fp_t*) malloc(13*13*384*sizeof(fp_t));
    for(i = 0; i < 384; i++) {
        memcpy(&relu3_file_content[i*13*13], conv3_output[i], 13*13*sizeof(fp_t));
    }
    write_pgm(relu3_file_content, 384*13, 13, "relu3_output.pgm");
    write_float(relu3_file_content, 384*13, 13, "relu3_output.float");
    free(relu3_file_content);
    #endif


    // conv4 13x13x384 -> 13x13x384
    #pragma omp parallel for private(i,j)
    for(i = 0; i < 384; i++) {
        convolution2d_cpu_3x3_s1_same(conv3_output[0], 13, 13, conv4_output[i], conv4_kernels[i*384], 0.0);

        for(j = 1; j < 383; j++) {
            convolution2d_cpu_3x3_s1_same(conv3_output[j], 13, 13, conv4_intermediate[omp_get_thread_num()], conv4_kernels[i*384+j], 0.0);
            add_channel2d_cpu(conv4_output[i], conv4_intermediate[omp_get_thread_num()], 13, 13);
        }
        convolution2d_cpu_3x3_s1_same(conv3_output[383], 13, 13, conv4_intermediate[omp_get_thread_num()], conv4_kernels[i*384+383], conv4_bias[i]);
        add_channel2d_cpu(conv4_output[i], conv4_intermediate[omp_get_thread_num()], 13, 13);
    }


    // make pgm of conv4 output
    #ifdef DEBUG
    fp_t* conv4_file_content = (fp_t*) malloc(13*13*384*sizeof(fp_t));
    for(i = 0; i < 384; i++) {
        memcpy(&conv4_file_content[i*13*13], conv4_output[i], 13*13*sizeof(fp_t));
    }
    write_pgm(conv4_file_content, 384*13, 13, "conv4_output.pgm");
    write_float(conv4_file_content, 384*13, 13, "conv4_output.float");
    free(conv4_file_content);
    #endif

    
    // relu4
    for(i = 0; i < 384; i++) {
        relu_cpu(conv4_output[i], 13, 13, conv4_output[i]);
    }

    // make pgm of relu4 output
    #ifdef DEBUG
    fp_t* relu4_file_content = (fp_t*) malloc(13*13*384*sizeof(fp_t));
    for(i = 0; i < 384; i++) {
        memcpy(&relu4_file_content[i*13*13], conv4_output[i], 13*13*sizeof(fp_t));
    }
    write_pgm(relu4_file_content, 384*13, 13, "relu4_output.pgm");
    write_float(relu4_file_content, 384*13, 13, "relu4_output.float");
    free(relu4_file_content);
    #endif

    #pragma omp parallel for private(i,j)
    // conv5 13x13x384 -> 13x13x256 
    for(i = 0; i < 256; i++) {
        convolution2d_cpu_3x3_s1_same(conv4_output[0], 13, 13, conv5_output[i], conv5_kernels[i*384], 0.0);

        for(j = 1; j < 383; j++) {
            convolution2d_cpu_3x3_s1_same(conv4_output[j], 13, 13, conv5_intermediate[omp_get_thread_num()], conv5_kernels[i*384+j], 0.0);
            add_channel2d_cpu(conv5_output[i], conv5_intermediate[omp_get_thread_num()], 13, 13);
        }
        convolution2d_cpu_3x3_s1_same(conv4_output[383], 13, 13, conv5_intermediate[omp_get_thread_num()], conv5_kernels[i*384+383], conv5_bias[i]);
        add_channel2d_cpu(conv5_output[i], conv5_intermediate[omp_get_thread_num()], 13, 13);
    }


    // make pgm of conv5 output
    #ifdef DEBUG
    fp_t* conv5_file_content = (fp_t*) malloc(13*13*256*sizeof(fp_t));
    for(i = 0; i < 256; i++) {
        memcpy(&conv5_file_content[i*13*13], conv5_output[i], 13*13*sizeof(fp_t));
    }
    write_pgm(conv5_file_content, 256*13, 13, "conv5_output.pgm");
    write_float(conv5_file_content, 256*13, 13, "conv5_output.float");
    free(conv5_file_content);
    #endif

    
    // relu5
    for(i = 0; i < 256; i++) {
        relu_cpu(conv5_output[i], 13, 13, conv5_output[i]);
    }

    // make pgm of relu5 output
    #ifdef DEBUG
    fp_t* relu5_file_content = (fp_t*) malloc(13*13*256*sizeof(fp_t));
    for(i = 0; i < 256; i++) {
        memcpy(&relu5_file_content[i*13*13], conv5_output[i], 13*13*sizeof(fp_t));
    }
    write_pgm(relu5_file_content, 256*13, 13, "relu5_output.pgm");
    write_float(relu5_file_content, 256*13, 13, "relu5_output.float");
    free(relu5_file_content);
    #endif


    // pool5 13x13x256 -> 6x6x256
    #pragma omp parallel for private(i)
    for(i = 0; i < 256; i++) {
        max_pooling2d_cpu_3x3_s2(conv5_output[i], 13, 13, pool5_output[i]);
    }

    // make pgm of pool2 output
    #ifdef DEBUG
    fp_t* pool5_file_content = (fp_t*) malloc(6*6*256*sizeof(fp_t));
    for(i = 0; i < 256; i++) {
        memcpy(&pool5_file_content[i*6*6], pool5_output[i], 6*6*sizeof(fp_t));
    }
    write_pgm(pool5_file_content, 256*6, 6, "pool5_output.pgm");
    write_float(pool5_file_content, 256*6, 6, "pool5_output.float");
    free(pool5_file_content);
    #endif

    
    // fc6 6x6x256 = 1x9216 -> 1x4096
    // merge S4 output
    for(i = 0; i < 256; i++) {
        memcpy(&pool5_output_merged[i*6*6], pool5_output[i], 6*6*sizeof(fp_t));
    }

    #pragma omp parallel for private(i)
    for(i = 0; i < omp_get_max_threads(); i++) {
        fully_connected_cpu(pool5_output_merged, 9216, fc6_output, 4096, fc6_kernel, fc6_bias, (4096/omp_get_max_threads())*omp_get_thread_num(), (4096/omp_get_max_threads())*(omp_get_thread_num()+1));
    }

    // make pgm fc6 output
    #ifdef DEBUG
    write_pgm(fc6_output, 1, 4096, "fc6_output.pgm");
    write_float(fc6_output, 1, 4096, "fc6_output.float");
    #endif
    

    // relu6
    relu_cpu(fc6_output, 1, 4096, fc6_output);

    // make pgm of relu5 output
    #ifdef DEBUG
    write_pgm(fc6_output, 1, 4096, "relu6_output.pgm");
    write_float(fc6_output, 1, 4096, "relu6_output.float");
    #endif


    // drop6
    // do nothing


    // fc7 1x4096 -> 1x4096
    #pragma omp parallel for private(i)
    for(i = 0; i < omp_get_max_threads(); i++) {
        fully_connected_cpu(fc6_output, 4096, fc7_output, 4096, fc7_kernel, fc7_bias, (4096/omp_get_max_threads())*omp_get_thread_num(), (4096/omp_get_max_threads())*(omp_get_thread_num()+1));
    }


    // make pgm fc7 output
    #ifdef DEBUG
    write_pgm(fc7_output, 1, 4096, "fc7_output.pgm");
    write_float(fc7_output, 1, 4096, "fc7_output.float");
    #endif


    // relu7
    relu_cpu(fc7_output, 1, 4096, fc7_output);

    // make pgm of relu7 output
    #ifdef DEBUG
    write_pgm(fc7_output, 1, 4096, "relu7_output.pgm");
    write_float(fc7_output, 1, 4096, "relu7_output.float");
    #endif

    
    // drop8
    // do nothing


    // fc8 1x4096 -> 1x1000    
    #pragma omp parallel for private(i)
    for(i = 0; i < omp_get_max_threads(); i++) {
        fully_connected_cpu(fc7_output, 4096, fc8_output, 1000, fc8_kernel, fc8_bias, (1000/omp_get_max_threads())*omp_get_thread_num(), (1000/omp_get_max_threads())*(omp_get_thread_num()+1));
    }

    // make pgm fc8 output
    #ifdef DEBUG
    write_pgm(fc8_output, 1, 1000, "fc8_output.pgm");
    write_float(fc8_output, 1, 1000, "fc8_output.float");
    #endif

    // prob
    softmax_cpu_single(fc8_output, 1, 1000, fc8_output);

    // make pgm prob
    #ifdef DEBUG
    write_pgm(fc8_output, 1, 1000, "prob_output.pgm");
    write_float(fc8_output, 1, 1000, "prob_output.float");
    #endif

    // free memory
    // input
    free(input[0]);
    free(input[1]);
    free(input[2]);
    free(input);

    // conv1
    for(i = 0; i < omp_get_max_threads(); i++) {
        free(conv1_intermediate[i]);
    }
    free(conv1_intermediate);

    for(i = 0; i < 96; i++) {
        free(conv1_output[i]);
    }
    free(conv1_output);

    // norm1
    for(i = 0; i < 96; i++) {
        free(norm1_output[i]);
    }
    free(norm1_output);

    // pool1
    for(i = 0; i < 96; i++) {
        free(pool1_output[i]);
    }
    free(pool1_output);

    // conv2
    for(i = 0; i < omp_get_max_threads(); i++) {
        free(conv2_intermediate[i]);
    }
    free(conv2_intermediate);

    for(i = 0; i < 256; i++) {
        free(conv2_output[i]);
    }
    free(conv2_output);

    // norm2
    for(i = 0; i < 256; i++) {
        free(norm2_output[i]);
    }
    free(norm2_output);

    // pool2
    for(i = 0; i < 256; i++) {
        free(pool2_output[i]);
    }
    free(pool2_output);

    // conv3
    for(i = 0; i < omp_get_max_threads(); i++) {
        free(conv3_intermediate[i]);
    }
    free(conv3_intermediate);

    for(i = 0; i < 384; i++) {
        free(conv3_output[i]);
    }
    free(conv3_output);

    // conv4
    for(i = 0; i < omp_get_max_threads(); i++) {
        free(conv4_intermediate[i]);
    }
    free(conv4_intermediate);

    for(i = 0; i < 384; i++) {
        free(conv4_output[i]);
    }
    free(conv4_output);

    // conv5
    for(i = 0; i < omp_get_max_threads(); i++) {
        free(conv5_intermediate[i]);
    }
    free(conv5_intermediate);

    for(i = 0; i < 256; i++) {
        free(conv5_output[i]);
    }
    free(conv5_output);

    // pool5
    for(i = 0; i < 256; i++) {
        free(pool5_output[i]);
    }
    free(pool5_output);

    // fc6
    free(pool5_output_merged);
    free(fc6_output);

    // fc7
    free(fc7_output);

    // fc8

    for(i = 0; i < 288; i++) {
        free(kernels[0][i]);
    }
    free(kernels[0]);

    for(i = 0; i < 24576; i++) {
        free(kernels[1][i]);
    }
    free(kernels[1]);

    for(i = 0; i < 98304; i++) {
        free(kernels[2][i]);
    }
    free(kernels[2]);

    for(i = 0; i < 147456; i++) {
        free(kernels[3][i]);
    }
    free(kernels[3]);

    for(i = 0; i < 98304; i++) {
        free(kernels[4][i]);
    }
    free(kernels[4]);

    free(kernels[5][0]);
    free(kernels[5]);
    free(kernels[6][0]);
    free(kernels[6]);
    free(kernels[7][0]);
    free(kernels[7]);
    free(kernels);

    for(i = 0; i < 8; i++) {
        free(biasses[i]);
    }
    free(biasses);
    
    // print prediction
    uint16_t* labels_pos;
    labels_pos = (uint16_t*) malloc(1000*sizeof(uint16_t));

    for(i = 0; i < 1000; i++) {
        labels_pos[i] = i;
    }

    sort_prediction(fc8_output, labels_pos, 1000);

    printf("prediction:\n");

    for(i = 0; i < 10; i++) {
        printf("%d %f %s\n", i+1, fc8_output[i], labels[labels_pos[i]]);
    }

    free(fc8_output);

    for(i = 0; i < 1000; i++) {
        free(labels[i]);
    }
    free(labels);

    free(labels_pos);

    return 0;
}


