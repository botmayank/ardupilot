/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 *       AP_Compass_Callib.cpp 
 *       Code by Siddharth Bharat Purohit. 3DRobotics Inc.
 *  Credit:
 *    Parts of the work are specifically Levenberg-Marquadt inplementation
 *    based on Efficient Java Matrix Library 
 *       by Peter Abeles <https://github.com/lessthanoptimal/ejml>
 *    distributed with Apache 2.0 license <http://www.apache.org/licenses/LICENSE-2.0>
 *
 */

#include "Compass.h"


#define NUM_PARAMS 4
#define NUM_SAMPLES 100

#define AIMED_FITNESS  1.0f     //desired max value of fitness
#define MAX_ITERS      10       //no. of iterations after which if convergence doesn't happen calib process is said to be failed
#define SAMPLE_RATE    5        //no. of samples/sec
#define MAX_OFF_VAL    1000     //this value makes sure no insane value get through (unused)
#define MIN_OFF_VAL   -1000     //(unused)
#define SAMPLE_DIST    50
#define GRADIENT       5
#define GRADIENT_POW_LIMIT 8    //highest power of GRADIENT to be reached, can be read as saturation limit too
/*  
    GRADIENT value specify the speed with which the optimiser will try to converge
    very high value means very low chance of convergence as the steps taken will
    be too large, while very low value will ensure the convergence will occur but
    may take huge amount of time. Striking balance with this factor is the key to
    -wards successful result in viable time period.
*/

#define MAX_RAD        500      //(unused)

#define JACOB_DELTA 0.000000001f

extern const AP_HAL::HAL& hal;
struct Calibration{
    double JTJ_LI[NUM_PARAMS*NUM_PARAMS];           //JTJ_LI = [Jacobian]*[Jacobian]^T + L*[Identity_Matrix]
    double JTFI[NUM_SAMPLES];                       //JTFI   = [Jacobian]^T * [Fitness_Matrix]
    double jacob[NUM_SAMPLES*NUM_PARAMS];           // [Jacobian]
    double sample_fitness[NUM_SAMPLES];             // [Fitness]
    double sphere_param[NUM_PARAMS];                // Parameters: Radius, Offset1, Offset2, Offset3
    Vector3f samples[NUM_SAMPLES];                  // Collected Magnetometer Samples
    uint8_t count;                                  // No. of Sample collected
    uint8_t passed;                                 // No. of times Square Sum fitness passed
    bool complete;                                  // Callibration Completed or not
    bool fault;                                     // Is there any fault:
                                                    // Singular Matrix Inversion 
                                                    // or Memory allocation failure

    uint8_t instance;                               // Magnetometer Instance no.
};


/* magnetometer Calibration Routine
 @known issues/improvements:
            -better way to let the user know of Calibration status
             but using console until maths and algos are finalised
*/
bool Compass::magnetometer_calib(void)
{
    uint8_t iters = 0;
    bool done = false;
    struct Calibration *calib;
    calib = NULL;
    calib = new struct Calibration[2];
    if(calib == NULL){
        return false;
    }
    // Initialise everything
    for(int instance=0; instance < get_count(); instance++){
        for(uint8_t cnt = 0; cnt < NUM_PARAMS; cnt++){
            calib[instance].sphere_param[cnt] = 20;         //initialising with any random value except 0 will do
        }
        calib[instance].passed = 0;
        calib[instance].count = 0;
        calib[instance].complete = false;
        calib[instance].fault = false;
        calib[instance].instance = instance; 
    }

    while((iters < MAX_ITERS) && !done){            //break when number of iterations are exceeded or
                                                    //callibration has completed
        iters++;
        for(int i=0;i < get_count(); i++){
            calib[i].count = 0;
        }

        collect_samples(calib);
        

        for(uint8_t instance = 0; instance < get_count(); instance++){

            if(calib[instance].complete){
                hal.console->printf("Calibration Completed!!!! I[%d]: Best Match: \nOff1: %.2f Off2: %.2f Off3: %.2f \n\n",
                                    instance, calib[instance].sphere_param[1],
                                    calib[instance].sphere_param[2],
                                    calib[instance].sphere_param[3]);
                continue;
            }

            process_samples(calib[instance]);

            if(calib[instance].fault){
                hal.console->printf("Critical Fault occured during sample processing...");
                return false;
            }
        }

        done = true;
        for(uint8_t instance = 0; instance < get_count(); instance++){
            if(!calib[instance].complete){
                done = false;
                break;
            }
        }
    }

    if(!done){
        hal.console->printf("\nCalibration Failed!!!!");
        return false;
    }
    
    delete[] calib;
    return true;
}

/*
    Process collected samples to generate closest parameters (Radius, Off1, Off2, Off3)
    Known Issues/Possible enhancements:
                    -check sanity of generated results probably by passing them through limits
*/
void Compass::process_samples(struct Calibration &calib)
{
    double global_best_f;
    bool ofb;
    calib.fault = false;

    if(calib.fault){
        return;
    }
    
    
    global_best_f = evaluatelm(calib);             //Evaluate Levenberg-Marquadt Iterations

    
    hal.console->printf("I[%d]: \nRad: %.2f Off1: %.2f Off2: %.2f Off3: %.2f fitness: %.5f \n\n",
                        calib.instance,
                        calib.sphere_param[0],
                        calib.sphere_param[1], 
                        calib.sphere_param[2],
                        calib.sphere_param[3],
                        global_best_f);

    //check if we are getting close
    if(global_best_f <= AIMED_FITNESS){
        calib.passed++;                      //total consecutive fitness test passed
        hal.console->printf("Good Fitness Test Passed:  %d\n",calib.passed);
    }else{
        calib.passed = 0;
    }
    //check if this is it
    if(calib.passed == 2){
        calib.complete = true;
    }else{
        calib.complete = false;
    }
}


/*
    Collect Raw samples from all available Magnetometers whenever distance between
    consecutive samples satisfies a lower limit
    
    Known Issues/Possible enhancements:
                    -Very Rudimentary implementation, needs a total make over
                    -Needs Timeout else will loop forever until sample buffer is filled
*/

void Compass::collect_samples(struct Calibration calib[])
{
    //collect Samples
    uint8_t c=0,sampling_over_cnt = 0;
    Vector3f distance;

    while(1){

        for(uint8_t instance = 0; instance < get_count(); instance++){
            c = calib[instance].count;

            if(c == NUM_SAMPLES){
                continue;
            }

            // Read a Sample from Magnetometer
            read();
            if (!healthy(instance)) {
                hal.console->print("not healthy      \n");
                continue;
            }
            const Vector3f &mag = get_field(instance);
            
            if( c >= 1){
                distance = calib[instance].samples[c - 1] - mag;
                
                if(distance.length() > SAMPLE_DIST){
                    calib[instance].samples[c] = mag;
                    if(validate_sample(calib[instance])){
                        c++;
                    }
                }
            }else{
                calib[instance].samples[c] = mag;
                c++;
            }


            if(c == 100){
                sampling_over_cnt++;            //Count for how many instances Calibration is over
            }


            calib[instance].count = c;
            hal.console->printf("[%d]  ",c);
        }

        hal.console->printf("\r");
        if(sampling_over_cnt == get_count()){
            break;
        }
        hal.scheduler->delay(1000/(SAMPLE_RATE));       //Delay before reading next sample so samples are
                                                        //not very close
    }

    hal.console->printf("Sampling Over \n");
}


/*
    Validates if sample should be utilised or not, currently returns
    true if new sample is exclusive from the rest.
    
    Known Issues/enhancements:
            -needs more conditions to ensure user rotates the vehicle
             in all directions
*/
bool Compass::validate_sample(struct Calibration &calib){
    for(uint8_t i = 0; i < calib.count; i++){
        if(calib.samples[i] == calib.samples[calib.count]){
            return false;
        }
    }
    return true;
}


/*
     Returns Squared Sum with set of fitness data(sample_fitness)
     as generated in sphere_fitness.
*/
double Compass::square_sum(struct Calibration &calib)
{
    double sqsum=0;
    for(uint16_t i=0; i < NUM_SAMPLES; i++){
        sqsum += calib.sample_fitness[i] * calib.sample_fitness[i];
    }
    return sqsum;
}

/* 
    Calculates fitness of sample points to sphere with calculated parameters
*/
void Compass::sphere_fitness(struct Calibration &calib)
{
    double a;
    if(abs(calib.sphere_param[0]) < 1){
        a = 1;
        calib.sphere_param[0] = 1;
    }else{
        a = 1/(calib.sphere_param[0] * calib.sphere_param[0]);
    }
    for(uint16_t i=0; i < NUM_SAMPLES; i++){
        calib.sample_fitness[i] = 1 - a * ((calib.samples[i].x + calib.sphere_param[1]) * (calib.samples[i].x + calib.sphere_param[1]) +
                                        (calib.samples[i].y + calib.sphere_param[2]) * (calib.samples[i].y + calib.sphere_param[2]) +
                                        (calib.samples[i].z + calib.sphere_param[3]) * (calib.samples[i].z + calib.sphere_param[3]));
    }
}


/*
    matrix inverse code only for 4x4 square matrix copied from 
    gluInvertMatrix implementation in 
    opengl for 4x4 matrices.

    @param     m,           input 4x4 matrix
    @param     invOut,      Output inverted 4x4 matrix
    @returns                false = matrix is Singular, true = matrix inversion successful
    Known Issues/ Possible Enhancements:
                -Will need a different implementation for more number
                 of parameters like in the case of addition of soft
                 iron calibration
*/
bool Compass::inverse4x4(double m[],double invOut[])
{
    double inv[16], det;
    uint8_t i;

    inv[0] = m[5]  * m[10] * m[15] - 
             m[5]  * m[11] * m[14] - 
             m[9]  * m[6]  * m[15] + 
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] - 
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] + 
              m[4]  * m[11] * m[14] + 
              m[8]  * m[6]  * m[15] - 
              m[8]  * m[7]  * m[14] - 
              m[12] * m[6]  * m[11] + 
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] - 
             m[4]  * m[11] * m[13] - 
             m[8]  * m[5] * m[15] + 
             m[8]  * m[7] * m[13] + 
             m[12] * m[5] * m[11] - 
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] + 
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] - 
               m[8]  * m[6] * m[13] - 
               m[12] * m[5] * m[10] + 
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] + 
              m[1]  * m[11] * m[14] + 
              m[9]  * m[2] * m[15] - 
              m[9]  * m[3] * m[14] - 
              m[13] * m[2] * m[11] + 
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] - 
             m[0]  * m[11] * m[14] - 
             m[8]  * m[2] * m[15] + 
             m[8]  * m[3] * m[14] + 
             m[12] * m[2] * m[11] - 
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] + 
              m[0]  * m[11] * m[13] + 
              m[8]  * m[1] * m[15] - 
              m[8]  * m[3] * m[13] - 
              m[12] * m[1] * m[11] + 
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] - 
              m[0]  * m[10] * m[13] - 
              m[8]  * m[1] * m[14] + 
              m[8]  * m[2] * m[13] + 
              m[12] * m[1] * m[10] - 
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] - 
             m[1]  * m[7] * m[14] - 
             m[5]  * m[2] * m[15] + 
             m[5]  * m[3] * m[14] + 
             m[13] * m[2] * m[7] - 
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] + 
              m[0]  * m[7] * m[14] + 
              m[4]  * m[2] * m[15] - 
              m[4]  * m[3] * m[14] - 
              m[12] * m[2] * m[7] + 
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] - 
              m[0]  * m[7] * m[13] - 
              m[4]  * m[1] * m[15] + 
              m[4]  * m[3] * m[13] + 
              m[12] * m[1] * m[7] - 
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] + 
               m[0]  * m[6] * m[13] + 
               m[4]  * m[1] * m[14] - 
               m[4]  * m[2] * m[13] - 
               m[12] * m[1] * m[6] + 
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + 
              m[1] * m[7] * m[10] + 
              m[5] * m[2] * m[11] - 
              m[5] * m[3] * m[10] - 
              m[9] * m[2] * m[7] + 
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - 
             m[0] * m[7] * m[10] - 
             m[4] * m[2] * m[11] + 
             m[4] * m[3] * m[10] + 
             m[8] * m[2] * m[7] - 
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + 
               m[0] * m[7] * m[9] + 
               m[4] * m[1] * m[11] - 
               m[4] * m[3] * m[9] - 
               m[8] * m[1] * m[7] + 
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - 
              m[0] * m[6] * m[9] - 
              m[4] * m[1] * m[10] + 
              m[4] * m[2] * m[9] + 
              m[8] * m[1] * m[6] - 
              m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    if (det == 0)
        return false;

    det = 1.0 / det;

    for (i = 0; i < 16; i++)
        invOut[i] = inv[i] * det;
    return true;
}

/*
    Generate Jacobian Matrix by changing each parameter
    by a very low value delta and noticing the change that
    happened in sphere fitness of sample data.
*/
void Compass::calc_jacob(struct Calibration &calib)
{
    double *temp;

    temp = new double[NUM_SAMPLES];
    if(temp == NULL){
        calib.fault = true;
        return;
    }


    sphere_fitness(calib);
    for(uint8_t i = 0; i < NUM_SAMPLES; i++){
        temp[i] = calib.sample_fitness[i];
    }



    for(uint8_t row=0 ; row < NUM_PARAMS; row++){
        calib.sphere_param[row] = calib.sphere_param[row] + JACOB_DELTA;
        sphere_fitness(calib);

        for(uint16_t col = 0; col < NUM_SAMPLES; col++){
            calib.jacob[row*NUM_SAMPLES + col] = temp[col] - calib.sample_fitness[col];
        }

        calib.sphere_param[row] = calib.sphere_param[row] - JACOB_DELTA;
    }


    delete[] temp;
}

/*
    calculates Transpose(Jacobian_Matrix)*Jacobian_Matrix + lambda*Identity_Matrix
    just doing matrix algebra for the optimiser
*/
void Compass::calc_JTJ_LI(struct Calibration &calib, double lambda)
{
    for(uint8_t i = 0;i < NUM_PARAMS; i++){
        for(uint8_t j = 0; j < NUM_PARAMS; j++){
            for(uint16_t k = 0;k < NUM_SAMPLES; k++){
                calib.JTJ_LI[i*NUM_PARAMS + j] += calib.jacob[i*NUM_SAMPLES + k] * calib.jacob[j*NUM_SAMPLES + k];
            }
        }
    }


    for(uint8_t diag=0; diag < NUM_PARAMS; diag++){
            calib.JTJ_LI[diag * NUM_PARAMS + diag] += lambda;
    }


    if(inverse4x4(calib.JTJ_LI, calib.JTJ_LI)){         //calc and return Inverse of JTJ_LI = [(JT).J + L.I]
        return;
    }else{
        calib.fault = true;             //register fault if matrix is singular
    }
}

/*
    calculates Transpose(Jacobian_Matrix)*Fitness_Matrix
    just doing matrix algebra for the optimiser
*/
void Compass::calc_JTFI(struct Calibration &calib)
{
    sphere_fitness(calib);
    for(uint8_t row = 0; row < NUM_PARAMS; row++){
        for(uint16_t col = 0; col < NUM_SAMPLES; col++){
            calib.JTFI[row] += calib.jacob[row*NUM_SAMPLES + col] * calib.sample_fitness[col];
        }
    }
}

/*
    do iterations of Levenberg_Marquadt on Samples
    
    Known Issues:
            -the iteration might go forever, addition of a timeout
             might solve the issue
*/
double Compass::evaluatelm(struct Calibration &calib)
{
    double lambda=1, last_fitness, global_best[NUM_SAMPLES];
    double global_best_f;       //global best fitness
    double cur_fitness;
    int16_t gradient_power = 0;

    sphere_fitness(calib);

    last_fitness = square_sum(calib);
    global_best_f = last_fitness;
    for(uint8_t i=0; i < NUM_PARAMS; i++){
        global_best[i] = calib.sphere_param[i];
    }
    while(gradient_power <= GRADIENT_POW_LIMIT){

        //Initialise everything
        for(uint16_t j = 0; j < (NUM_PARAMS * NUM_SAMPLES); j++)
            calib.jacob[j] = 0;
        for(uint16_t j = 0; j < (NUM_PARAMS * NUM_PARAMS); j++)
            calib.JTJ_LI[j] = 0;
        for(uint16_t j = 0;j < NUM_PARAMS; j++)
            calib.JTFI[j] = 0;


        calc_jacob(calib);          //step  1
        if(calib.fault){
          return -1;
        }
         
        calc_JTJ_LI(calib,lambda);  //step  2
        if(calib.fault){
          return -1;
        }

        calc_JTFI(calib);           //step  3
        

        // Final Step
        // [New_Params] = [Old_Params] + { [J^T * J + LI]^(-1) * [J^T * Fi] }
        // T = Transpose, J= Jacobian, Fi = Fitness Matrix

        for(uint8_t row=0; row < NUM_PARAMS; row++){
            for(uint8_t col=0; col < NUM_PARAMS; col++){
                calib.sphere_param[row] += calib.JTFI[col] * calib.JTJ_LI[row*4 + col];
            }
        }
        //LM iteration complete
        
        //pass generated result through conditions
        sphere_fitness(calib);
        cur_fitness = square_sum(calib);
        
        if(cur_fitness >= last_fitness){
            lambda *= GRADIENT;
            gradient_power++;
        }else{
            lambda /= GRADIENT;
            last_fitness = cur_fitness;
            gradient_power--;
        }


        if(cur_fitness < global_best_f){
            global_best_f = cur_fitness;
            for(uint8_t j = 0; j < NUM_PARAMS; j++)
                global_best[j] = calib.sphere_param[j];
        }

        if(cur_fitness < AIMED_FITNESS/2){
            break;
        }
    }


    for(uint8_t i=0; i < NUM_PARAMS; i++){
        calib.sphere_param[i] = global_best[i];
    }

    return global_best_f;
}