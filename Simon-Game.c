
 
/**
* 
* Drew Wan
* Video Link: https://youtu.be/_tNlj_9OsvQ

* IMPLEMENTATION OF EMBEDDEDML IN LEARNING DEVICE ORIENTATION
**/
 
/**
******************************************************************************
* @file    DataLog/Src/main.c
* @author  Central Labs
* @version V1.1.1
* @date    06-Dec-2016
* @brief   Main program body
******************************************************************************
* @attention
*
* <h2><center>&copy; COPYRIGHT(c) 2016 STMicroelectronics</center></h2>
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*   1. Redistributions of source code must retain the above copyright notice,
*      this list of conditions and the following disclaimer.
*   2. Redistributions in binary form must reproduce the above copyright notice,
*      this list of conditions and the following disclaimer in the documentation
*      and/or other materials provided with the distribution.
*   3. Neither the name of STMicroelectronics nor the names of its contributors
*      may be used to endorse or promote products derived from this software
*      without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
******************************************************************************
*/
 
/* Includes ------------------------------------------------------------------*/
 
#include <string.h> /* strlen */
#include <stdio.h>  /* sprintf */
#include <math.h>   /* trunc */
#include "embeddedML.h"
#include "main.h"
 
#include "datalog_application.h"
#include "usbd_cdc_interface.h"
 
/* FatFs includes component */
#include "ff_gen_drv.h"
#include "sd_diskio.h"
 
/* Private typedef -----------------------------------------------------------*/
 
/* Private define ------------------------------------------------------------*/
 
/* Data acquisition period [ms] */
#define DATA_PERIOD_MS (10)
 
#define NUMBER_TEST_CYCLES 10
#define CLASSIFICATION_ACC_THRESHOLD 1
#define CLASSIFICATION_DISC_THRESHOLD 1.05
#define Z_ACCEL_THRESHOLD 300
#define START_POSITION_INTERVAL 3000
#define TRAINING_CYCLES 2000
#define LED_BLINK_INTERVAL 200
 
/* Baseline Project Adding
*
*/
#define ANGLE_MAG_MAX_THRESHOLD 30
#define MAX_ROTATION_ACQUIRE_CYCLES 400
 
 
//#define NOT_DEBUGGING
 
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
 
/* SendOverUSB = 0  --> Save sensors data on SDCard (enable with double click) */
/* SendOverUSB = 1  --> Send sensors data via USB */
uint8_t SendOverUSB = 1;
 
USBD_HandleTypeDef USBD_Device;
static volatile uint8_t MEMSInterrupt = 0;
static volatile uint8_t no_H_HTS221 = 0;
static volatile uint8_t no_T_HTS221 = 0;
static volatile uint8_t no_GG = 0;
 
static RTC_HandleTypeDef RtcHandle;
static void *LSM6DSM_X_0_handle = NULL;
static void *LSM6DSM_G_0_handle = NULL;
static void *LSM303AGR_X_0_handle = NULL;
static void *LSM303AGR_M_0_handle = NULL;
static void *LPS22HB_P_0_handle = NULL;
static void *LPS22HB_T_0_handle = NULL;
static void *HTS221_H_0_handle = NULL;
static void *HTS221_T_0_handle = NULL;
static void *GG_handle = NULL;
 
/* Private function prototypes -----------------------------------------------*/
 
static void Error_Handler(void);
static void RTC_Config(void);
static void RTC_TimeStampConfig(void);
static void initializeAllSensors(void);
 
/* Private functions ---------------------------------------------------------*/
 
static volatile uint8_t hasTrained = 0;
unsigned int training_cycles = TRAINING_CYCLES;
 
/*baseline project getAngular Velocity added
*
*/
 
void getAngularVelocity(void *handle_g, int *xyz){
​uint8_t id;
​SensorAxes_t angular_velocity;
​uint8_t status;
 
​BSP_GYRO_Get_Instance(handle_g, &id);
​BSP_GYRO_IsInitialized(handle_g, &status);
 
​if (status ==1){
​​if (BSP_GYRO_Get_Axes(handle_g, &angular_velocity) == COMPONENT_ERROR){
​​​angular_velocity.AXIS_X = 0;
​​​angular_velocity.AXIS_Y = 0;
​​​angular_velocity.AXIS_Z = 0;
 
 
​​}
​​xyz[0] = (int) angular_velocity.AXIS_X;
​​xyz[1] = (int) angular_velocity.AXIS_Y;
​​xyz[2] = (int) angular_velocity.AXIS_Z;
​}
}
 
void stable_softmax(float *x, float *y) {
​int size = 3;
​float multiplier = 1.0;
 
​int i;
 
​//softmax implemented as square law algorithm to be accommodate numerical precision requirements
 
​y[0] = (x[0] * x[0] * multiplier)
​​​/ ((x[0] * x[0] * multiplier) + (x[1] * x[1] * multiplier)
​​​​​+ (x[2] * x[2] * multiplier));
​y[1] = (x[1] * x[1] * multiplier)
​​​/ ((x[0] * x[0] * multiplier) + (x[1] * x[1] * multiplier)
​​​​​+ (x[2] * x[2] * multiplier));
​y[2] = (x[2] * x[2] * multiplier)
​​​/ ((x[0] * x[0] * multiplier) + (x[1] * x[1] * multiplier)
​​​​​+ (x[2] * x[2] * multiplier));
 
​for (i = 0; i < size; i++) {
​​if (x[i] < 0.0)
​​​y[i] = y[i] * -1.0;
​}
}
 
void motion_softmax(int size, float *x, float *y) {
​float norm;
 
​norm = sqrt((x[0] * x[0]) + (x[1] * x[1]) + (x[2] * x[2]));
​y[0] = abs(x[0]) / norm;
​y[1] = abs(x[1]) / norm;
​y[2] = abs(x[2]) / norm;
 
​int i;
​for (i = 0; i < size; i++) {
​​if (x[i] < 0.0)
​​​y[i] = y[i] * -1.0;
​}
}
 
void LED_Code_Blink(int count) {
 
​int i;
 
​/*
​ * Alert signal of rapid blinks indicating code to be shown
​ */
​for (i = 0; i < 7; i++) {
​​BSP_LED_On(LED1);
​​HAL_Delay(20);
​​BSP_LED_Off(LED1);
​​HAL_Delay(50);
​}
 
​/*
​ * Code indicated by number of slow blinks
​ */
 
​if (count != 0) {
​​HAL_Delay(1000);
​​for (i = 0; i < count; i++) {
​​​BSP_LED_On(LED1);
​​​HAL_Delay(500);
​​​BSP_LED_Off(LED1);
​​​HAL_Delay(500);
​​}
​}
 
​/*
​ * Alert signal of rapid blinks indicating end of code
​ */
​for (i = 0; i < 7; i++) {
​​BSP_LED_On(LED1);
​​HAL_Delay(20);
​​BSP_LED_Off(LED1);
​​HAL_Delay(30);
​}
 
}
 
void LED_Notification_Blink(int count) {
 
​int i;
 
​/*
​ * Rapid blink notification
​ */
 
​for (i = 0; i < count; i++) {
​​BSP_LED_On(LED1);
​​HAL_Delay(20);
​​BSP_LED_Off(LED1);
​​HAL_Delay(50);
​}
}
 
void getAccel(void *handle, int *xyz) {
​uint8_t id;
​SensorAxes_t acceleration;
​uint8_t status;
 
​BSP_ACCELERO_Get_Instance(handle, &id);
 
​BSP_ACCELERO_IsInitialized(handle, &status);
 
​if (status == 1) {
​​if (BSP_ACCELERO_Get_Axes(handle, &acceleration) == COMPONENT_ERROR) {
​​​acceleration.AXIS_X = 0;
​​​acceleration.AXIS_Y = 0;
​​​acceleration.AXIS_Z = 0;
​​}
 
​​xyz[0] = (int) acceleration.AXIS_X;
​​xyz[1] = (int) acceleration.AXIS_Y;
​​xyz[2] = (int) acceleration.AXIS_Z;
​}
}
 
/*
* Note : Feature_Extraction_State_0() sets Z-axis acceleration, ttt_3 = 0
*/
 
void Feature_Extraction_State_0(void *handle, int * ttt_1, int * ttt_2,
​​int * ttt_3, int * ttt_mag_scale) {
 
​int ttt[3];
​int ttt_initial[3];
​int axis_index;
​float accel_mag;
​char  msg[128];
 
 
​/*
​ * Acquire acceleration values prior to motion
​ */
 
​getAccel(handle, ttt_initial);
 
​sprintf(msg, "\r\nStart Motion to new Orientation when LED On");
​CDC_Fill_Buffer((uint8_t *) msg, strlen(msg));
​BSP_LED_On(LED1);
 
​HAL_Delay(2000);
 
​sprintf(msg, "\r\nEnd Motion");
​CDC_Fill_Buffer((uint8_t *) msg, strlen(msg));
​HAL_Delay(1000);
 
​getAccel(handle, ttt);
 
 
​accel_mag = 0;
​​for (axis_index = 0; axis_index < 3; axis_index++) {
​​​accel_mag = accel_mag + pow((ttt[axis_index] - ttt_initial[axis_index]), 2);
​​}
 
​accel_mag = sqrt(accel_mag);
​*ttt_1 = ttt[0] - ttt_initial[0];
​*ttt_2 = ttt[1] - ttt_initial[1];
​*ttt_3 = ttt[2] - ttt_initial[2];
​*ttt_mag_scale = (int)(accel_mag);
 
​BSP_LED_Off(LED1);
​return;
}
 
/*
* Feature_Extraction_State_1() determines a second orientation after
* the action of Feature_Extraction_State_0().
*/
 
/*Baseline project edits after ttt[3]
*
*/
void Feature_Extraction_State_1(void *handle_g, int * ttt_1, int * ttt_2,
​​int * ttt_3, int * ttt_mag_scale) {
​int ttt[3], ttt_state_0[3], ttt_initial[3], ttt_offset[3];
​char msg1[128];
​int axis_index, sample_index;
​float rotate_angle[3];
​float angle_mag, angle_mag_max_threshold;
​float Tsample;
 
 
​ttt_state_0[0] = *ttt_1;
​ttt_state_0[1] = *ttt_2;
 
​angle_mag_max_threshold = ANGLE_MAG_MAX_THRESHOLD;
 
​Tsample = (float)(DATA_PERIOD_MS)/1000;
 
​for (axis_index = 0; axis_index < 3; axis_index++) {
​​ttt[axis_index] = 0;
​​rotate_angle[axis_index] = 0;
​}
 
​getAngularVelocity(handle_g, ttt_offset);
 
​/*
​ * Notify user to initiate second motion to rotate vertical up or down
​ */
 
 
​sprintf(msg1, "\r\nStart Second State Motion to New Orientation when LED On");
​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​BSP_LED_On(LED1);
 
​for (sample_index = 0; sample_index < MAX_ROTATION_ACQUIRE_CYCLES; sample_index++){
 
​​for (axis_index = 0; axis_index < 3; axis_index++){
​​​​ttt_initial[axis_index] = ttt[axis_index] - ttt_offset[axis_index];
​​}
​​HAL_Delay(DATA_PERIOD_MS);
​​getAngularVelocity(handle_g, ttt);
​​for (axis_index = 0; axis_index < 3; axis_index++){
​​​ttt[axis_index] = ttt[axis_index] - ttt_offset[axis_index];
​​​}
 
​​ttt_initial[2] = 0;
​​ttt[2] = 0;
 
​​for (axis_index = 0; axis_index < 3; axis_index++){
​​​rotate_angle[axis_index] = rotate_angle[axis_index] + (float)((ttt_initial[axis_index] + ttt[axis_index]) * Tsample / 2);
​​}
 
​​angle_mag = 0;
 
​​for (axis_index = 0; axis_index < 3; axis_index++) {
​​​angle_mag = angle_mag + pow((rotate_angle[axis_index]), 2);
​​}
 
​​angle_mag = sqrt(angle_mag) / 1000;
 
​​if (angle_mag >= angle_mag_max_threshold){
​​​break;
​​}
 
 
​}
 
 
 
​/*
​ * Retain initial values of acceleration for X and Y axes
​ */
 
​/*
​ * Detect SensorTile in inverted position
​ *
​ * Compute new feature according to value of Z-axis acceleration
​ * and orientation
​ *
​ * X and Y axis accelerations remain unchanged.  However, Z axis
​ * acceleration is assigned either to the average of X and Y
​ * axis acceleration or to zero.
​ */
 
​*ttt_3 = 0;
 
​*ttt_mag_scale = (int) (angle_mag * 100);
 
​sprintf(msg1, " \r\n");
 
 
 
​sprintf(msg1, "\r\nMotion with Angle Mag of %i degrees complete, Now Return to Next Start Position, ", (int)(angle_mag));
​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​BSP_LED_Off(LED1);
​HAL_Delay(3000);
​return;
 
}
 
void printOutput_ANN(ANN *net, int input_state, int * error) {
 
​char dataOut[256] = { };
​int i, loc, count;
​float point = 0.0;
​float rms_output, mean_output, mean_output_rem, next_max;
​float classification_metric;
 
​/*
​ * Initialize error state
​ */
 
​*error = 0;
 
​count = 0;
​mean_output = 0;
​for (i = 0; i < net->topology[net->n_layers - 1]; i++) {
​​mean_output = mean_output + (net->output[i]);
​​if (net->output[i] > point && net->output[i] > 0.1) {
​​​point = net->output[i];
​​​loc = i;
​​}
​​count++;
​}
 
​next_max = 0;
​for (i = 0; i < net->topology[net->n_layers - 1]; i++) {
​​if (i == loc) {
​​​continue;
​​}
​​if (net->output[i] > next_max && net->output[i] > 0.1) {
​​​next_max = net->output[i];
​​}
​}
 
​mean_output = (mean_output) / (count);
 
​count = 0;
​mean_output_rem = 0;
​for (i = 0; i < net->topology[net->n_layers - 1]; i++) {
​​mean_output_rem = mean_output_rem + (net->output[i]);
​​if (i == loc) {
​​​continue;
​​}
​​count++;
​}
 
​mean_output_rem = (mean_output_rem) / (count);
 
​rms_output = 0;
 
​for (i = 0; i < net->topology[net->n_layers - 1]; i++) {
​​rms_output = rms_output + pow((net->output[i] - mean_output), 2);
​}
 
​rms_output = sqrt(rms_output / count);
​if (rms_output != 0) {
​​classification_metric = (point - mean_output) / rms_output;
​} else {
​​classification_metric = 0;
​}
 
​if (loc != input_state) {
​​rms_output = 0;
​​classification_metric = 0;
​​point = 0;
​​mean_output = 0;
​​mean_output_rem = 0;
​}
 
​sprintf(dataOut, "\r\nState %i\tMax %i\tMean %i\t\tZ-score %i\tOutputs",
​​​loc, (int) (100 * point), (int) (100 * mean_output),
​​​(int) (100 * classification_metric));
​CDC_Fill_Buffer((uint8_t *) dataOut, strlen(dataOut));
 
​for (i = 0; i < net->topology[net->n_layers - 1]; i++) {
​​sprintf(dataOut, "\t%i", (int) (100 * net->output[i]));
​​CDC_Fill_Buffer((uint8_t *) dataOut, strlen(dataOut));
​}
 
​if (loc != input_state) {
​​*error = 1;
​​sprintf(dataOut, "\t Classification Error");
​​CDC_Fill_Buffer((uint8_t *) dataOut, strlen(dataOut));
​}
 
​if ((loc == input_state)
​​​&& ((classification_metric < CLASSIFICATION_ACC_THRESHOLD)
​​​​​|| ((point / next_max) < CLASSIFICATION_DISC_THRESHOLD))) {
​​*error = 1;
​​sprintf(dataOut, "\t Classification Accuracy Limit");
​​CDC_Fill_Buffer((uint8_t *) dataOut, strlen(dataOut));
​}
 
}
 
void TrainOrientation(void *handle,void *handle_g, ANN *net) {
 
​uint8_t id, id_g;
​SensorAxes_t acceleration, angular_velocity;
​uint8_t status, status_g;
​float training_data[6][3];
​float training_dataset[6][8][3];
​int ttt_initial_max[3];
​float XYZ[3];
​float xyz[3];
​float test_NN[3];
​char msg1[256];
​int num_train_data_cycles;
​int i, j, k, m, n, r, error, net_error;
​int ttt_1, ttt_2, ttt_3, ttt_mag_scale;
 
​BSP_ACCELERO_Get_Instance(handle, &id);
​BSP_ACCELERO_IsInitialized(handle, &status);
 
​BSP_GYRO_Get_Instance(handle_g, &id_g);
​BSP_GYRO_IsInitialized(handle_g, &status_g);
 
​if (BSP_ACCELERO_Get_Axes(handle, &acceleration) == COMPONENT_ERROR){
​​acceleration.AXIS_X = 0;
​​acceleration.AXIS_Y = 0;
​​acceleration.AXIS_Z = 0;
​​}
 
​if (status == 1 && status_g == 1){
​​if(BSP_GYRO_Get_Axes(handle_g, &angular_velocity) == COMPONENT_ERROR){
​​​angular_velocity.AXIS_X = 0;
​​​angular_velocity.AXIS_Y = 0;
​​​angular_velocity.AXIS_Z = 0;
​​}
​}
 
 
​​sprintf(msg1, "\r\n\r\n\r\nTraining Start in 2 seconds ..");
​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​BSP_LED_Off(LED1);
​​HAL_Delay(2000);
 
​​/*
​​ * Maximum of 8 cycles
​​ */
​​num_train_data_cycles = 1;
 
​​for (k = 0; k < num_train_data_cycles; k++) {
​​​for (i = 0; i < 6; i++) {
 
​​​​sprintf(msg1, "\r\nMove to Start Position - Wait for LED On");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​HAL_Delay(START_POSITION_INTERVAL);
 
​​​​switch (i) {
​​​​HAL_Delay(1000);
 
​​​case 0:
 
​​​​sprintf(msg1, "\r\nMove to Orientation 1 on LED On");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
 
​​​​Feature_Extraction_State_0(handle, &ttt_1, &ttt_2, &ttt_3,
​​​​​​&ttt_mag_scale);
 
​​​​Feature_Extraction_State_1(handle_g, &ttt_1, &ttt_2, &ttt_3,
​​​​​​​​&ttt_mag_scale);
 
​​​​break;
 
​​​case 1:
 
​​​​sprintf(msg1, "\r\nMove to Orientation 2 on LED On");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
 
​​​​Feature_Extraction_State_0(handle, &ttt_1, &ttt_2, &ttt_3,
​​​​​​&ttt_mag_scale);
 
​​​​Feature_Extraction_State_1(handle_g, &ttt_1, &ttt_2, &ttt_3,
​​​​​​​​&ttt_mag_scale);
 
​​​​break;
 
​​​case 2:
​​​​sprintf(msg1, "\r\nMove to Orientation 3 on LED On");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
 
​​​​Feature_Extraction_State_0(handle, &ttt_1, &ttt_2, &ttt_3,
​​​​​​&ttt_mag_scale);
 
​​​​Feature_Extraction_State_1(handle_g, &ttt_1, &ttt_2, &ttt_3,
​​​​​​​​&ttt_mag_scale);
 
​​​​break;
 
​​​case 3:
​​​​sprintf(msg1, "\r\nMove to Orientation 4 on LED On");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
 
​​​​Feature_Extraction_State_0(handle, &ttt_1, &ttt_2, &ttt_3,
​​​​​​&ttt_mag_scale);
 
​​​​Feature_Extraction_State_1(handle_g, &ttt_1, &ttt_2, &ttt_3,
​​​​​​​​&ttt_mag_scale);
 
​​​​break;
 
​​​case 4:
​​​​sprintf(msg1, "\r\nMove to Orientation 5 on LED On");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
 
​​​​Feature_Extraction_State_0(handle, &ttt_1, &ttt_2, &ttt_3,
​​​​​​&ttt_mag_scale);
 
​​​​Feature_Extraction_State_1(handle_g, &ttt_1, &ttt_2, &ttt_3,
​​​​​​​​&ttt_mag_scale);
 
​​​​break;
 
​​​case 5:
​​​​sprintf(msg1, "\r\nMove to Orientation 6 on LED On");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
 
​​​​Feature_Extraction_State_0(handle, &ttt_1, &ttt_2, &ttt_3,
​​​​​​&ttt_mag_scale);
 
​​​​Feature_Extraction_State_1(handle_g, &ttt_1, &ttt_2, &ttt_3,
​​​​​​​​&ttt_mag_scale);
 
​​​​break;
 
​​​​}
 
​​​​sprintf(msg1, "\r\nAccel %i\t\%i\%i", ttt_1, ttt_2, ttt_3);
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
 
​​​​XYZ[0] = (float) ttt_1;
​​​​XYZ[1] = (float) ttt_2;
​​​​XYZ[2] = (float) ttt_3;
 
​​​​motion_softmax(net->topology[0], XYZ, xyz);
 
​​​​training_dataset[i][k][0] = xyz[0];
​​​​training_dataset[i][k][1] = xyz[1];
​​​​training_dataset[i][k][2] = xyz[2];
 
​​​​sprintf(msg1, "\r\n Softmax Input \t");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​for (r = 0; r < 3; r++) {
​​​​​sprintf(msg1, "\t%i", (int) XYZ[r]);
​​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​}
​​​​sprintf(msg1, "\r\n Softmax Output\t");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​for (r = 0; r < 3; r++) {
​​​​​sprintf(msg1, "\t%i", (int) (100 * xyz[r]));
​​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​}
​​​​sprintf(msg1, "\r\n\r\n");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​}
​​}
 
​​/*
​​ * Enter NN training
​​ */
 
​​float _Motion_1[6] = { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
​​float _Motion_2[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, 0.0 };
​​float _Motion_3[6] = { 0.0, 0.0, 1.0, 0.0, 0.0, 0.0 };
​​float _Motion_4[6] = { 0.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
​​float _Motion_5[6] = { 0.0, 0.0, 0.0, 0.0, 1.0, 0.0 };
​​float _Motion_6[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 1.0 };
 
​​sprintf(msg1, "\r\n\r\nTraining Start\r\n");
​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
 
​​for (k = 0; k < num_train_data_cycles; k++) {
 
​​​i = 0;
​​​while (i < training_cycles) {
​​​​for (j = 0; j < 6; j++) {
 
 
​​​​​if ((i % 20 == 0 && i < 100) || i % 100 == 0) {
​​​​​​char print_train_time[128];
​​​​​​sprintf(print_train_time,
​​​​​​​​"\r\n\r\nTraining Epochs: %d\r\n", i);
​​​​​​CDC_Fill_Buffer((uint8_t *) print_train_time,
​​​​​​​​strlen(print_train_time));
 
​​​​​​LED_Code_Blink(0);
 
​​​​​​net_error = 0;
​​​​​​for (m = 0; m < 6; m++) {
​​​​​​​run_ann(net, training_dataset[m][k]);
​​​​​​​printOutput_ANN(net, m, &error);
​​​​​​​if (error == 1) {
​​​​​​​​net_error = 1;
​​​​​​​}
​​​​​​}
​​​​​​sprintf(msg1, "\r\nError State: %i\r\n",
​​​​​​​​net_error);
​​​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​​​if (net_error == 0) {
​​​​​​​return;
​​​​​​}
 
​​​​​}
 
​​​​​switch (j) {
 
​​​​​case 0:
​​​​​​train_ann(net, training_dataset[j][k], _Motion_1);
​​​​​​break;
​​​​​case 1:
​​​​​​train_ann(net, training_dataset[j][k], _Motion_2);
​​​​​​break;
​​​​​case 2:
​​​​​​train_ann(net, training_dataset[j][k], _Motion_3);
​​​​​​break;
​​​​​case 3:
​​​​​​train_ann(net, training_dataset[j][k], _Motion_4);
​​​​​​break;
​​​​​case 4:
​​​​​​train_ann(net, training_dataset[j][k], _Motion_5);
​​​​​​break;
​​​​​case 5:
​​​​​​train_ann(net, training_dataset[j][k], _Motion_6);
​​​​​​break;
​​​​​default:
​​​​​​break;
​​​​​}
​​​​​i++;
​​​​​HAL_Delay(5);
​​​​}
 
​​​}
 
​​}
 
 
​if (SendOverUSB) /* Write data on the USB */
​{
​​//sprintf( dataOut, "\n\rAX: %d, AY: %d, AZ: %d", (int)acceleration.AXIS_X, (int)acceleration.AXIS_Y, (int)acceleration.AXIS_Z );
​​//CDC_Fill_Buffer(( uint8_t * )dataOut, strlen( dataOut ));
​}
 
​if (net_error == 0){
​​LED_Code_Blink(0);
​​LED_Code_Blink(0);
​} else {
​​LED_Code_Blink(1);
​​LED_Code_Blink(1);
​}
 
​sprintf(msg1, "\r\n\r\nTraining Complete, Now Start Test Motions\r\n");
​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​return;
}
 
int Accel_Gyro_Sensor_Handler(void *handle, void *handle_g, ANN *net, int prev_loc) {
​uint8_t id, id_g;
​SensorAxes_t acceleration;
​SensorAxes_t angular_velocity;
​uint8_t status;
​uint8_t status_g;
​float xyz[3];
​float XYZ[3];
​float point;
​int i, j, k, loc, motion, start;
​int ttt_1, ttt_2, ttt_3, ttt_mag_scale;
​char msg1[128];
​char msg2[128];
​char msg3[128];
​char msg4[128];
 
​BSP_ACCELERO_Get_Instance(handle, &id);
 
​BSP_ACCELERO_IsInitialized(handle, &status);
 
​BSP_GYRO_Get_Instance(handle_g, &id_g);
​BSP_GYRO_IsInitialized(handle_g, &status_g);
 
​if (status == 1 && status_g == 1) {
​​if (BSP_GYRO_Get_Axes(handle_g, &angular_velocity) == COMPONENT_ERROR) {
​​​angular_velocity.AXIS_X = 0;
​​​angular_velocity.AXIS_Y = 0;
​​​angular_velocity.AXIS_Z = 0;
​​}
 
​​/*
​​ * Perform limited number of NN execution and prediction cycles.
​​ * Upon return, training will be repeased
​​ */
 
​​k = 0;
​​motion = 0;
​​while (k < NUMBER_TEST_CYCLES) {
 
​​​BSP_LED_Off(LED1);
 
​​​sprintf(msg1, "\n\rMove to Start Position and complete motions in order- Wait for LED On");
​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​if((loc - 1 == motion)){
​​​​motion = motion + 1;
​​​} else {
​​​​motion = motion;
​​​​}
 
​​​sprintf(msg1, "\n\rStart motion %i, Loc Motion is %i", (int) motion, (int) loc);
​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
 
 
 
 
 
​​​HAL_Delay(START_POSITION_INTERVAL);
 
​​​Feature_Extraction_State_0(handle, &ttt_1, &ttt_2, &ttt_3,
​​​​​&ttt_mag_scale);
 
​​​Feature_Extraction_State_1(handle_g, &ttt_1, &ttt_2, &ttt_3,
​​​​​&ttt_mag_scale);
 
           XYZ[0] = (float) ttt_1;
           XYZ[1] = (float) ttt_2;
           XYZ[2] = (float) ttt_3;
 
​​​motion_softmax(net->topology[0], XYZ, xyz);
 
 
​​​run_ann(net, xyz);
 
​​​point = 0.0;
​​​loc = -1;
 
​​​for (i = 0; i < net->topology[net->n_layers - 1]; i++) {
​​​​if (net->output[i] > point && net->output[i] > 0.1) {
​​​​​point = net->output[i];
​​​​​loc = i;
​​​​}
​​​}
 
​​​if (loc == -1) {
​​​​LED_Code_Blink(0);
​​​} else {
​​​​LED_Code_Blink(loc + 1);
​​​}
 
 
 
 
 
​​​switch (motion){
​​​case 0:
​​​​sprintf(msg4, "\n\rMotion 1 Complete");
​​​​CDC_Fill_Buffer((uint8_t *) msg4, strlen(msg4));
​​​​break;
​​​case 1:
​​​​sprintf(msg4, "\n\rMotion 2 Complete");
​​​​CDC_Fill_Buffer((uint8_t *) msg4, strlen(msg4));
​​​​break;
​​​case 2:
​​​​sprintf(msg4, "\n\rMotion 3 Complete");
​​​​CDC_Fill_Buffer((uint8_t *) msg4, strlen(msg4));
​​​​break;
​​​case 3:
​​​​sprintf(msg4, "\n\rMotion 4 Complete");
​​​​CDC_Fill_Buffer((uint8_t *) msg4, strlen(msg4));
​​​​break;
​​​case 4:
​​​​sprintf(msg4, "\n\rMotion 5 Complete");
​​​​CDC_Fill_Buffer((uint8_t *) msg4, strlen(msg4));
​​​​break;
​​​case 5:
​​​​sprintf(msg4, "\n\rMotion 6 Complete, YAY!");
​​​​CDC_Fill_Buffer((uint8_t *) msg4, strlen(msg4));
​​​​break;
 
​​​}
 
 
​​​switch (loc) {
​​​case 0:
​​​​sprintf(msg1, "\n\rNeural Network Classification - Motion 1");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​break;
​​​case 1:
​​​​sprintf(msg1, "\n\rNeural Network Classification - Motion 2");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​break;
​​​case 2:
​​​​sprintf(msg1, "\n\rNeural Network Classification - Motion 3");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​break;
​​​case 3:
​​​​sprintf(msg1, "\n\rNeural Network Classification - Motion 4");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​break;
​​​case 4:
​​​​sprintf(msg1, "\n\rNeural Network Classification - Motion 5");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​break;
​​​case 5:
​​​​sprintf(msg1,
​​​​​​"\n\rNeural Network Classification - Orientation 6");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​break;
​​​case -1:
​​​​sprintf(msg1, "\n\rNeural Network Classification - ERROR");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​break;
​​​default:
​​​​sprintf(msg1, "\n\rNeural Network Classification - NULL");
​​​​CDC_Fill_Buffer((uint8_t *) msg1, strlen(msg1));
​​​​break;
​​​}
​​if (motion == 5){
​​​sprintf(msg3, "\n\rYAY you did it!");
​​​CDC_Fill_Buffer((uint8_t *) msg3, strlen(msg3));
​​​k = 100;
​​} else{
​​​k = k + 1;
​​}
 
​​}
​}
​return prev_loc;
}
 
 
int main(void) {
​uint32_t msTick, msTickPrev = 0;
​uint8_t doubleTap = 0;
​char msg2[128];
​int i;
 
​/* STM32L4xx HAL library initialization:
​ - Configure the Flash prefetch, instruction and Data caches
​ - Configure the Systick to generate an interrupt each 1 msec
​ - Set NVIC Group Priority to 4
​ - Global MSP (MCU Support Package) initialization
​ */
​HAL_Init();
 
​/* Configure the system clock */
​SystemClock_Config();
 
​if (SendOverUSB) {
​​/* Initialize LED */
​​BSP_LED_Init(LED1);
​}
#ifdef NOT_DEBUGGING
​else
​{
​​/* Initialize LEDSWD: Cannot be used during debug because it overrides SWDCLK pin configuration */
​​BSP_LED_Init(LEDSWD);
​​BSP_LED_Off(LEDSWD);
​}
#endif
 
​/* Initialize RTC */
​RTC_Config();
​RTC_TimeStampConfig();
 
​/* enable USB power on Pwrctrl CR2 register */
​HAL_PWREx_EnableVddUSB();
 
​if (SendOverUSB) /* Configure the USB */
​{
​​/*** USB CDC Configuration ***/
​​/* Init Device Library */
​​USBD_Init(&USBD_Device, &VCP_Desc, 0);
​​/* Add Supported Class */
​​USBD_RegisterClass(&USBD_Device, USBD_CDC_CLASS);
​​/* Add Interface callbacks for AUDIO and CDC Class */
​​USBD_CDC_RegisterInterface(&USBD_Device, &USBD_CDC_fops);
​​/* Start Device Process */
​​USBD_Start(&USBD_Device);
​} else /* Configure the SDCard */
​{
​​DATALOG_SD_Init();
​}
​HAL_Delay(200);
 
​/* Configure and disable all the Chip Select pins */
​Sensor_IO_SPI_CS_Init_All();
 
​/* Initialize and Enable the available sensors */
​initializeAllSensors();
​enableAllSensors();
 
​/* Notify user */
 
​sprintf(msg2, "\n\rEmbeddedML Motion Pattern Classification\r\n");
​CDC_Fill_Buffer((uint8_t *) msg2, strlen(msg2));
 
​sprintf(msg2, "\n\rDouble Tap to start training");
​CDC_Fill_Buffer((uint8_t *) msg2, strlen(msg2));
 
​//---EMBEDDED ANN---
​float weights[81] = { 0.680700, 0.324900, 0.607300, 0.365800, 0.693000,
​​​0.527200, 0.754400, 0.287800, 0.592300, 0.570900, 0.644000,
​​​0.416500, 0.249200, 0.704200, 0.598700, 0.250300, 0.632700,
​​​0.372900, 0.684000, 0.661200, 0.230300, 0.516900, 0.770900,
​​​0.315700, 0.756000, 0.293300, 0.509900, 0.627800, 0.781600,
​​​0.733500, 0.509700, 0.382600, 0.551200, 0.326700, 0.781000,
​​​0.563300, 0.297900, 0.714900, 0.257900, 0.682100, 0.596700,
​​​0.467200, 0.339300, 0.533600, 0.548500, 0.374500, 0.722800,
​​​0.209100, 0.619400, 0.635700, 0.300100, 0.715300, 0.670800,
​​​0.794400, 0.766800, 0.349000, 0.412400, 0.619600, 0.353000,
​​​0.690300, 0.772200, 0.666600, 0.254900, 0.402400, 0.780100,
​​​0.285300, 0.697700, 0.540800, 0.222800, 0.693300, 0.229800,
​​​0.698100, 0.463500, 0.201300, 0.786500, 0.581400, 0.706300,
​​​0.653600, 0.542500, 0.766900, 0.411500 };
​float dedw[81];
​float bias[15];
​unsigned int network_topology[3] = { 3, 9, 6 };
​float output[6];
 
​ANN net;
​net.weights = weights;
​net.dedw = dedw;
​net.bias = bias;
​net.topology = network_topology;
​net.n_layers = 3;
​net.n_weights = 81;
​net.n_bias = 15;
​net.output = output;
 
​for (i = 0; i < 15; i++){
​​bias[i] = 0.5;
​}
​for (i = 0; i < 6; i++){
​​output[i] = 0.0;
​}
​for (i = 0; i < 81; i++){
​​dedw[i] = 0.0;
​}
 
​//OPTIONS
​net.eta = 0.13;     //Learning Rate
​net.beta = 0.01;    //Bias Learning Rate
​net.alpha = 0.25;   //Momentum Coefficient
​net.output_activation_function = &relu2;
​net.hidden_activation_function = &relu2;
 
​init_ann(&net);
​//---------------------
 
​int loc = -1;
​while (1) {
​​/* Get sysTick value and check if it's time to execute the task */
​​msTick = HAL_GetTick();
​​if (msTick % DATA_PERIOD_MS == 0 && msTickPrev != msTick) {
​​​msTickPrev = msTick;
 
​​​if (SendOverUSB) {
​​​​BSP_LED_On(LED1);
​​​}
 
​​​//RTC_Handler( &RtcHandle );
 
​​​if (hasTrained){
​​​​loc = Accel_Gyro_Sensor_Handler(LSM6DSM_X_0_handle,LSM6DSM_G_0_handle, &net, loc);
​​​​/*
​​​​ * Upon return from Accel_Sensor_Handler, initiate retraining.
​​​​ */
​​​​hasTrained = 0;
​​​​sprintf(msg2, "\n\r\n\rDouble Tap to start a new training session");
​​​​CDC_Fill_Buffer((uint8_t *) msg2, strlen(msg2));
​​​}
 
​​​if (SendOverUSB) {
​​​​BSP_LED_Off(LED1);
​​​}
 
​​}
 
​​/* Check LSM6DSM Double Tap Event  */
​​if (!hasTrained) {
​​​BSP_ACCELERO_Get_Double_Tap_Detection_Status_Ext(LSM6DSM_X_0_handle,
​​​​​&doubleTap);
​​​if (doubleTap) { /* Double Tap event */
​​​​LED_Code_Blink(0);
​​​​TrainOrientation(LSM6DSM_X_0_handle,LSM6DSM_G_0_handle, &net);
​​​​hasTrained = 1;
​​​}
​​}
 
​​/* Go to Sleep */
​​__WFI();
​}
}
 
/**
* @brief  Initialize all sensors
* @param  None
* @retval None
*/
static void initializeAllSensors(void) {
​if (BSP_ACCELERO_Init(LSM6DSM_X_0, &LSM6DSM_X_0_handle) != COMPONENT_OK) {
​​while (1)
​​​;
​}
 
​if (BSP_GYRO_Init(LSM6DSM_G_0, &LSM6DSM_G_0_handle) != COMPONENT_OK) {
​​while (1)
​​​;
​}
 
​if (BSP_ACCELERO_Init(LSM303AGR_X_0, &LSM303AGR_X_0_handle)
​​​!= COMPONENT_OK) {
​​while (1)
​​​;
​}
 
​if (BSP_MAGNETO_Init(LSM303AGR_M_0, &LSM303AGR_M_0_handle)
​​​!= COMPONENT_OK) {
​​while (1)
​​​;
​}
 
​if (BSP_PRESSURE_Init(LPS22HB_P_0, &LPS22HB_P_0_handle) != COMPONENT_OK) {
​​while (1)
​​​;
​}
 
​if (BSP_TEMPERATURE_Init(LPS22HB_T_0, &LPS22HB_T_0_handle)
​​​!= COMPONENT_OK) {
​​while (1)
​​​;
​}
 
​if (BSP_TEMPERATURE_Init(HTS221_T_0, &HTS221_T_0_handle)
​​​== COMPONENT_ERROR) {
​​no_T_HTS221 = 1;
​}
 
​if (BSP_HUMIDITY_Init(HTS221_H_0, &HTS221_H_0_handle) == COMPONENT_ERROR) {
​​no_H_HTS221 = 1;
​}
 
​/* Inialize the Gas Gauge if the battery is present */
​if (BSP_GG_Init(&GG_handle) == COMPONENT_ERROR) {
​​no_GG = 1;
​}
 
​//if(!SendOverUSB)
​//{
​/* Enable HW Double Tap detection */
​BSP_ACCELERO_Enable_Double_Tap_Detection_Ext(LSM6DSM_X_0_handle);
​BSP_ACCELERO_Set_Tap_Threshold_Ext(LSM6DSM_X_0_handle,
​LSM6DSM_TAP_THRESHOLD_MID);
​//}
 
}
 
/**
* @brief  Enable all sensors
* @param  None
* @retval None
*/
void enableAllSensors(void) {
​BSP_ACCELERO_Sensor_Enable(LSM6DSM_X_0_handle);
​BSP_GYRO_Sensor_Enable(LSM6DSM_G_0_handle);
​BSP_ACCELERO_Sensor_Enable(LSM303AGR_X_0_handle);
​BSP_MAGNETO_Sensor_Enable(LSM303AGR_M_0_handle);
​BSP_PRESSURE_Sensor_Enable(LPS22HB_P_0_handle);
​BSP_TEMPERATURE_Sensor_Enable(LPS22HB_T_0_handle);
​if (!no_T_HTS221) {
​​BSP_TEMPERATURE_Sensor_Enable(HTS221_T_0_handle);
​​BSP_HUMIDITY_Sensor_Enable(HTS221_H_0_handle);
​}
 
}
 
/**
* @brief  Disable all sensors
* @param  None
* @retval None
*/
void disableAllSensors(void) {
​BSP_ACCELERO_Sensor_Disable(LSM6DSM_X_0_handle);
​BSP_ACCELERO_Sensor_Disable(LSM303AGR_X_0_handle);
​BSP_GYRO_Sensor_Disable(LSM6DSM_G_0_handle);
​BSP_MAGNETO_Sensor_Disable(LSM303AGR_M_0_handle);
​BSP_HUMIDITY_Sensor_Disable(HTS221_H_0_handle);
​BSP_TEMPERATURE_Sensor_Disable(HTS221_T_0_handle);
​BSP_TEMPERATURE_Sensor_Disable(LPS22HB_T_0_handle);
​BSP_PRESSURE_Sensor_Disable(LPS22HB_P_0_handle);
}
 
/**
* @brief  Configures the RTC
* @param  None
* @retval None
*/
static void RTC_Config(void) {
​/*##-1- Configure the RTC peripheral #######################################*/
​RtcHandle.Instance = RTC;
 
​/* Configure RTC prescaler and RTC data registers */
​/* RTC configured as follow:
​ - Hour Format    = Format 12
​ - Asynch Prediv  = Value according to source clock
​ - Synch Prediv   = Value according to source clock
​ - OutPut         = Output Disable
​ - OutPutPolarity = High Polarity
​ - OutPutType     = Open Drain */
​RtcHandle.Init.HourFormat = RTC_HOURFORMAT_12;
​RtcHandle.Init.AsynchPrediv = RTC_ASYNCH_PREDIV;
​RtcHandle.Init.SynchPrediv = RTC_SYNCH_PREDIV;
​RtcHandle.Init.OutPut = RTC_OUTPUT_DISABLE;
​RtcHandle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
​RtcHandle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
 
​if (HAL_RTC_Init(&RtcHandle) != HAL_OK) {
 
​​/* Initialization Error */
​​Error_Handler();
​}
}
 
/**
* @brief  Configures the current time and date
* @param  None
* @retval None
*/
static void RTC_TimeStampConfig(void) {
 
​RTC_DateTypeDef sdatestructure;
​RTC_TimeTypeDef stimestructure;
 
​/*##-3- Configure the Date using BCD format ################################*/
​/* Set Date: Monday January 1st 2000 */
​sdatestructure.Year = 0x00;
​sdatestructure.Month = RTC_MONTH_JANUARY;
​sdatestructure.Date = 0x01;
​sdatestructure.WeekDay = RTC_WEEKDAY_MONDAY;
 
​if (HAL_RTC_SetDate(&RtcHandle, &sdatestructure, FORMAT_BCD) != HAL_OK) {
 
​​/* Initialization Error */
​​Error_Handler();
​}
 
​/*##-4- Configure the Time using BCD format#################################*/
​/* Set Time: 00:00:00 */
​stimestructure.Hours = 0x00;
​stimestructure.Minutes = 0x00;
​stimestructure.Seconds = 0x00;
​stimestructure.TimeFormat = RTC_HOURFORMAT12_AM;
​stimestructure.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
​stimestructure.StoreOperation = RTC_STOREOPERATION_RESET;
 
​if (HAL_RTC_SetTime(&RtcHandle, &stimestructure, FORMAT_BCD) != HAL_OK) {
​​/* Initialization Error */
​​Error_Handler();
​}
}
 
/**
* @brief  Configures the current time and date
* @param  hh the hour value to be set
* @param  mm the minute value to be set
* @param  ss the second value to be set
* @retval None
*/
void RTC_TimeRegulate(uint8_t hh, uint8_t mm, uint8_t ss) {
 
​RTC_TimeTypeDef stimestructure;
 
​stimestructure.TimeFormat = RTC_HOURFORMAT12_AM;
​stimestructure.Hours = hh;
​stimestructure.Minutes = mm;
​stimestructure.Seconds = ss;
​stimestructure.SubSeconds = 0;
​stimestructure.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
​stimestructure.StoreOperation = RTC_STOREOPERATION_RESET;
 
​if (HAL_RTC_SetTime(&RtcHandle, &stimestructure, FORMAT_BIN) != HAL_OK) {
​​/* Initialization Error */
​​Error_Handler();
​}
}
 
/**
* @brief  EXTI line detection callbacks
* @param  GPIO_Pin: Specifies the pins connected EXTI line
* @retval None
*/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
​MEMSInterrupt = 1;
}
 
/**
* @brief  This function is executed in case of error occurrence
* @param  None
* @retval None
*/
static void Error_Handler(void) {
 
​while (1) {
​}
}
 
#ifdef  USE_FULL_ASSERT
 
/**
* @brief  Reports the name of the source file and the source line number
*   where the assert_param error has occurred
* @param  file: pointer to the source file name
* @param  line: assert_param error line source number
* @retval None
*/
void assert_failed( uint8_t *file, uint32_t line )
{
 
​/* User can add his own implementation to report the file name and line number,
​ ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
 
​while (1)
​{}
}
 
#endif
 
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
 
 
 