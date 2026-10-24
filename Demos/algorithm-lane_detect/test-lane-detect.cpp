#include "lane_detect.h"

#include <sys/time.h>

#include <opencv2/opencv.hpp>
#include <rknn_api.h>

#include "utils.h"

/**
 * Main function for testing lane detection.
 * Pipeline:
 *      1. Initialize lane detection model
 *      2. Read input image
 *      3. Run lane detection
 *      4. Release lane detection model
 *      5. Convert results to lanes
 *      6. Draw lanes on the image
 */

static double _get_us(struct timeval t) {
    return (t.tv_sec * 1000000 + t.tv_usec);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <model_path> <image_path>\n", argv[0]);
        return -1;
    }

    int ret;
    char *model_path = argv[1];
    char *image_path = argv[2];

    // Initialize lane detection model
    lane_detect_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ret = lane_detect_init(&ctx, model_path);
    if (ret != 0) {
        printf("Failed to initialize lane detection model.\n");
        return -1;
    }

    // Read the input image
    printf("Reading image from %s\n", image_path);
    cv::Mat input_image = cv::imread(image_path, cv::IMREAD_COLOR);
    if (input_image.empty()) {
        printf("Failed to read image from %s.\n", image_path);
        return -1;
    }

    // Run lane detection
    //  - Model Results: vector of detect_result
    //  - Postprocess Info: postprocess_info struct containing scaling and cropping info
    std::vector<detect_result> model_results;
    postprocess_info info;
    float score_threshold = 0.41f;  // Default score threshold
    int nms_threshold = 50;       // Default NMS threshold

    timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    for (int i = 0; i < 10; ++i) {  // Run multiple times for performance measurement
        ret = lane_detect_run(&ctx, input_image, model_results, info, score_threshold, nms_threshold);
        if (ret != 0) {
            printf("Failed to run lane detection.\n");
            lane_detect_release(&ctx);
            return -1;
        }
    }
    gettimeofday(&end_time, NULL);
    double elapsed_time = (_get_us(end_time) - _get_us(start_time)) / 10.0;  // Average time per run
    printf("Average lane detection time: %.2f ms\n", elapsed_time / 1000.0);
    
    // ret = lane_detect_run(&ctx, input_image, model_results, info, score_threshold, nms_threshold);
    // if (ret != 0) {
    //     printf("Failed to run lane detection.\n");
    //     lane_detect_release(&ctx);
    //     return -1;
    // }

    printf("Detected %zu lanes\n", model_results.size());

    // Release lane detection model
    ret = lane_detect_release(&ctx);
    if (ret != 0) {
        printf("Failed to release lane detection model.\n");
        return -1;
    }

    // Convert results to lanes
    std::vector<lane> lanes;
    ret = results_to_lanes(model_results, lanes, info);
    if (ret != 0) {
        printf("Failed to convert results to lanes.\n");
        return -1;
    }
    printf("Turn %zu result to %zu lanes.\n", model_results.size(), lanes.size());

    // Draw lanes on the image and save the result
    cv::Mat output_image = input_image.clone();
    draw_line(output_image, lanes);
    cv::imwrite("result.jpg", output_image);
    return 0;
}
