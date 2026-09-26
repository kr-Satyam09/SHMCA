#pragma once
#include<vector>
#include<cstdlib>



inline bool chance(int percent)
{
    return (std::rand() % 100) < percent;
}

inline void runChaosEngine(std::vector<satellite*> &sats, std::vector<Debris> &debrisList,int %nextDebrisId,std::ofstream &logFile,double simTime){
    if(sats.empty()) 
        return;


}