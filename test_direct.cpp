#include <fstream>
#include <iostream>

using namespace std;

int main()
{
	fstream myDev("/dev/sbull", ios::in | ios::out | ios::trunc | ios::binary);
	fstream myFile0("/home/sailendra/loopbackfile0.img", ios::in | ios::out | ios::trunc | ios::binary);
	fstream myFile1("/home/sailendra/loopbackfile1.img", ios::in | ios::out | ios::trunc | ios::binary);
	fstream myFile2("/home/sailendra/loopbackfile2.img", ios::in | ios::out | ios::trunc | ios::binary);
	
	char buff[16];
	char buf0[16], buf1[16], buf2[16];


	cout<<"writing Hello World to 0 offset"<<endl;
    // Add the characters "Hello World" to the file
    myDev << "Hello World hello hello";

	myDev.read(buff, 15);
	myFile0.read(buf0, 15);
	myFile1.read(buf1, 15);
	myFile2.read(buf2, 15);
	buff[15] = 0;
	buf0[15] = 0;
	buf1[15] = 0;
	buf2[15] = 0;

	cout<<buff<<endl;
	cout<<buf0<<endl;
	cout<<buf1<<endl;
	cout<<buf2<<endl;

    // Seek to 6 characters from the beginning of the file
    myFile0.seekg(6, ios::beg);
     
    // Read the next 5 characters from the file into a buffer
    char A[6];
    myFile0.read(A, 5);
     
    // End the buffer with a null terminating character
    A[5] = 0;
     
    // Output the contents read from the file and close it
    cout << A << endl;
     

	myDev.close();
    myFile0.close();
	myFile1.close();
	myFile2.close();
}