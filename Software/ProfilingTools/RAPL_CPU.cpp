//COMPILATION INSTRUCTIONS
//-> g++ RAPL_CPU.cpp -o RAPL -std=c++17 -pthread -lstdc++fs
//EXECUTION
//-> ./RAPL ConfigFile BinaryToMonitor BinaryParameters

#include <iostream>
#include <fstream>
#include <algorithm>
#include <map>
#include <filesystem>
#include <vector>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
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

map<string,long> getEnergy_uj(string subdomain){
    
    char delimiter = ',';
    vector<string> subd;
    size_t pos = 0;
    string path = "/sys/class/powercap/intel-rapl";
    string cadena = "intel-rapl";
    string Fenergy_uj = "/energy_uj";
    string Fname = "/name";
    string dir, name, ruta, file, energia, pathing;
    ifstream eFile;
    char dom;
    map<string,long> energy;

    // Meter subdominios en el vector
    while ((pos = subdomain.find(delimiter)) != string::npos) {
        subd.push_back(subdomain.substr(0, pos));
        subdomain.erase(0, pos + 1);
    }
    subd.push_back(subdomain);

    // Para cada subdominio
    for (int i=0; i<subd.size(); i++) {
        dom = subd[i].substr(0,subd[i].find('-')).c_str()[0];
        name = subd[i].substr(subd[i].find('-')+1);
        // Recorremos el directorio
        for (const auto & entry : filesystem::directory_iterator(path)){
            dir = entry.path().filename();
            pathing = entry.path();
            // Si la entrada se llama intel-rapl
            if(dir.find(cadena)!= string::npos){
                // Si coincide con el índice del subdominio, entramos
                if(dir.back() == dom){
                    ruta = pathing + Fname;
                    eFile.open(ruta);
                    if(eFile.is_open()){
                        getline(eFile, file);
                        file = file.substr(0,file.find('-'));
                        eFile.close();
                    }
                    // Si es el subdominio que buscamos
                    if(file == name){
                        ruta = pathing + Fenergy_uj;
                        // Accedemos al fichero energy_uj
                        eFile.open(ruta);
                        if(eFile.is_open()){
                            getline(eFile,energia);
                            eFile.close();
                            // Almacenamos energía
                            energy.insert({subd[i],stol(energia)});
                        }
                    // Si no
                    }else {
                        // Recorremos el directorio
                        for(const auto & entry : filesystem::directory_iterator(pathing)){
                            dir = entry.path().filename();
                            pathing = entry.path();
                            // Si la entrada se llama intel-rapl
                            if(dir.find(cadena)!= string::npos){
                                // Recorremos el directorio
                                ruta = pathing + Fname;
                                eFile.open(ruta);
                                if(eFile.is_open()){
                                    getline(eFile, file);
                                    file = file.substr(0,file.find('-'));
                                    eFile.close();
                                }
                                // Si es el subdominio que buscamos
                                if(file == name){
                                    ruta = pathing + Fenergy_uj;
                                    // Accedemos al fichero energy_uj
                                    eFile.open(ruta);
                                    if(eFile.is_open()){
                                        getline(eFile,energia);
                                        eFile.close();
                                        // Almacenamos energía
                                        energy.insert({subd[i],stol(energia)});
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return energy;
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
    map<string,long> Eanterior;
    map<string,long> Eactual;
    map<string,float> EnerAcum;
    pthread_t hilo;
    float Ener, minEner;
    struct timespec tactual, tsiguiente, tdormido, tlimite, tinicio;
    string param, subdom, fichero;
    fichero = argv[1];
    param = argv[2];
    for(int i=3; i < argc; i++){
        param = param +" "+ argv[i];
    }
    
    mp = readConfig(fichero);
    //Intervalo de tiempo para hacer los muestreos
    int sampleT = stoi(mp.find("SAMPLE_TIME")->second);
    tdormido = ms2timespec(sampleT);
    //Energía mínima para incluir muestra
    minEner = stoi(mp.find("MIN_POWER")->second)*pow(10,-6);
    

    //Fichero de salida
    cFile.open(mp.find("OUTPUT_FILE")->second,ios::out | ios::trunc);
    cFile << mp.find("SUBDOMAIN")->first <<"="<<mp.find("SUBDOMAIN")->second << endl;
    cFile << "timestamp" << ";" << "subdominio" << ";" <<  "energia" << ";"<<  "potencia" << ";"<< endl;

    tlimite = ms2timespec(stoi(mp.find("POST_EXEC_TIME")->second));
    clock_gettime(CLOCK_MONOTONIC,&tactual);
    tlimite = addTimeSpec(tactual, tlimite);
    tinicio = tactual;
    clock_gettime(CLOCK_MONOTONIC,&tactual);
    tsiguiente = addTimeSpec(tactual,tdormido);
    Eanterior = getEnergy_uj(mp.find("SUBDOMAIN")->second);
    for (auto itr = EnerAcum.begin(); itr != EnerAcum.end(); ++itr) {
        EnerAcum.insert({itr->first,0.0});
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
        Eactual = getEnergy_uj(mp.find("SUBDOMAIN")->second);
        for (auto itr = Eactual.begin(); itr != Eactual.end(); ++itr) {
            subdom = itr->first;
            Ener = (itr->second - Eanterior[itr->first])*pow(10,-6);
            EnerAcum[itr->first] = Ener + EnerAcum[itr->first];
            //Si no llega al mínimo de energia de corte se desecha la muestra
            if(Ener >= minEner){
                cFile << i*sampleT << ";" << subdom << ";" <<  Ener << ";"<<  (Ener/(sampleT*pow(10,-3))) << ";"<< endl;
            }
        }
        Eanterior = Eactual;
        i++;
    }
    cFile.close();
    cout << "FIN" << endl;
    return 0;
}