#include "stdafx.h"

using namespace std; 
const unsigned long long size = TOTAL_SIZE; 
unsigned long long a[TOTAL_SIZE]; 

int cpp_stream() 
{ 
    fstream myfile; 
    myfile = fstream("file.binary", ios::out | ios::binary); 
    //Here would be some error handling 
    for(int i = 0; i < 32; ++i){ 
        //Some calculations to fill a[] 
        myfile.write((char*)&a,size*sizeof(unsigned long long)); 
    } 
    myfile.close(); 
	return 0;
} 
