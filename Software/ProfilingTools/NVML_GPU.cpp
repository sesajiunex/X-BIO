//COMPILATION INSTRUCTIONS
//-> nvcc NVML-GPU.cpp -o NVML -lpthread -lstdc++fs -lnvidia-ml
//EXECUTION
//-> ./NVML ConfigFile BinaryToMonitor BinaryParameters

#include <iostream>
#include <fstream>
#include <algorithm>
#include <map>
#include <filesystem>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <nvml.h>
using namespace std;

bool flag = false; // Variable global para usar de bandera del hilo

map<string,string> readConfig(string fichero)
{
    map<string,string> kv; // Mapa donde almacenaremos los parámetros de configuración
    ifstream cFile;
    cFile.open(fichero);
    if (cFile.is_open())
    {
        string line;
        while(getline(cFile, line)){
            // Eliminamos los espacios en blanco
            line.erase(remove_if(line.begin(), line.end(), [](unsigned char x){return isspace(x);}),line.end());
            // Saltamos las líneas de comentarios
            if(line[0] == '#' || line.empty())
                continue;
            auto delimiterPos1 = line.find('=');
            auto delimiterPos2 = line.find('#');
            auto delimiterPos12 = delimiterPos2 - delimiterPos1;
            auto key = line.substr(0, delimiterPos1);
            auto value = line.substr(delimiterPos1+1,delimiterPos12-1);
            kv.insert({key,value});
        }
    }
    else {
        cerr << "Couldn't open config file for reading.\n";
    }
    cFile.close();
    return kv;
}

void *execProgram(void *param){
    flag = false;
    string parameter;
    parameter =*((string *)param);
    string cmd = "./"+parameter;
    system(cmd.c_str());
    flag = true;
}

struct timespec addTimeSpec(struct timespec src1, struct timespec src2) {
        struct timespec dst;
        if (src2.tv_nsec + src1.tv_nsec > 1000000000) {
                dst.tv_sec = src1.tv_sec + src2.tv_sec + 1;
                dst.tv_nsec =  src2.tv_nsec + src1.tv_nsec - 1000000000;
        }
        else {
                dst.tv_nsec =  src2.tv_nsec + src1.tv_nsec;
                dst.tv_sec  = src1.tv_sec + src2.tv_sec;
        }
        return dst;
}

struct timespec ms2timespec(int src) {
        struct timespec dst;
        dst.tv_sec=src/1000;
        dst.tv_nsec=(src%1000)*1000000;
        return dst;
}

int timespec2ms(struct timespec src) {
    return src.tv_sec*1000+src.tv_nsec/1000000;
}

int main(int argc, char *argv[])
{
    ofstream cFile;
    map<string,string> mp;
    pthread_t hilo;
    float Ener, minEner;
    struct timespec tactual, tsiguiente, tdormido, tlimite, tinicio;
    string param, subdom, fichero;
    fichero = argv[1];
    param = argv[2];
    for(int i=3; i < argc; i++){
        param = param +" "+ argv[i];
    }

    nvmlReturn_t result;
    nvmlDevice_t device;
    int GPU_id = 0;

    mp = readConfig(fichero);
    //Intervalo de tiempo para hacer los muestreos
    int sampleT = stoi(mp.find("SAMPLE_TIME")->second);
    tdormido = ms2timespec(sampleT);
    //Energía mínima para incluir muestra
    minEner = stoi(mp.find("MIN_POWER")->second)*pow(10,-6);

    //Fichero de salida
    cFile.open(mp.find("OUTPUT_FILE")->second,ios::out | ios::trunc);
    cFile << mp.find("GPU")->first << "=" << mp.find("GPU")->second << endl;
    cFile << "timestamp" << ";" << "GPU" << ";" <<  "energia" << ";"<<  "potencia" << ";"<< endl;

    tlimite = ms2timespec(stoi(mp.find("POST_EXEC_TIME")->second));
    clock_gettime(CLOCK_MONOTONIC,&tactual);
    tlimite = addTimeSpec(tactual, tlimite);
    tinicio = tactual;
    clock_gettime(CLOCK_MONOTONIC,&tactual);
    tsiguiente = addTimeSpec(tactual,tdormido);
    GPU_id = stoi(mp.find("GPU")->second);

    printf("Starting monitoring GPU %d with a timestamp %d\n", GPU_id, sampleT);
    result = nvmlInit();
    if (NVML_SUCCESS != result)
    {
                printf("Failed to initialize NVML: %s\n", nvmlErrorString(result));
                return 1;
    }
    result = nvmlDeviceGetHandleByIndex(GPU_id, &device); //primer parametro = GPU a usar
    if (NVML_SUCCESS != result)
    {
                printf("Failed to get handle for device %i: %s\n", 0, nvmlErrorString(result));
                return 1;
    }
    unsigned int power_usage = 0;
    float power = 0;
    float power_prev = 0;
    float power_avg = 0;
    float energy = 0;
    float energy_acc = 0;
    result = nvmlDeviceGetPowerUsage(device, &power_usage);
    power_prev = power_usage/1000.0;
    if (NVML_SUCCESS != result)
    {
                printf("Failed to get initial power measurement: %s\n", nvmlErrorString(result));
                return 1;
    }

    pthread_create(&hilo, NULL, execProgram, &param);

    int i = 0;
    bool noIF = true;
    flag = false;
    //Mientras el programa no haya acabado su ejecución y no se haya alcanzado el tiempo límite de post-ejecución
    //Registramos las muestras en el fichero de salida
    while(!flag || (tlimite.tv_sec > tsiguiente.tv_sec)||((tlimite.tv_sec == tsiguiente.tv_sec)&&(tlimite.tv_nsec >= tsiguiente.tv_nsec))){
        //Fragmento de post-ejecución
        if(noIF){
            tlimite = ms2timespec(stoi(mp.find("POST_EXEC_TIME")->second));
            clock_gettime(CLOCK_MONOTONIC,&tactual);
            tlimite = addTimeSpec(tactual, tlimite);
            if(flag){
                noIF = false;
                double postT;
                postT= (timespec2ms(tactual)-timespec2ms(tinicio))*pow(10,-3);
                cout << "POST_EXEC" <<";"<< postT << endl;
                cFile << "POST_EXEC" <<";"<< postT << endl;
            }
        }
        //Fragmento de ejecución
        clock_nanosleep (CLOCK_MONOTONIC, TIMER_ABSTIME,&tsiguiente, NULL);
        tsiguiente = addTimeSpec(tsiguiente,tdormido);

        nvmlDeviceGetPowerUsage(device, &power_usage);
        //printf("%d %f\n", power_usage, power_usage/1000.0);
        power = power_usage/1000.0;
        power_avg = (power + power_prev)/2.0;
        energy = power_avg * (sampleT/1000.0);
        energy_acc += energy;

        if(energy >= minEner)
                cFile << i*sampleT << ";" << GPU_id << ";" <<  energy << ";"<<  power_avg << ";"<< endl;

        power_prev = power;
        i++;
    }
    cFile.close();
    result = nvmlShutdown();
    if (NVML_SUCCESS != result)
    {
             printf("Failed to close NVML: %s\n", nvmlErrorString(result));
             return 1;
    }

    cout << "FIN" << endl;


    return 0;
}
