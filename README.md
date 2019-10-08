# Use Direct3D 12 Compute Shader in C (Basic)
Use Direct3D 12 Compute Shader in C (Basic)

<br />

This is a simple sample that describes how to use Direct3D 12 compute shader in the C programming language. Why C? Because the C-style COM APIs for Direct3D 12 contain no magical wrappers, it will fully demonstrate the whole process in detail without concealment.

The function of this sample is very simple. It calculates the sum of each *`int`* element in the source buffer with 10, and stores the result to the corresponding location in the destination buffer.

The genenral process is as follows:

1. Initialize all the necessary assets.
1. Initialize the command list and the command queue
1. Create the source buffer object and the destination buffer object.
1. Do the compute operation and fetch the result.
1. Release all the resources.

<br />

The demo project is built with Visual Studio 2019 community. Before you start, you may have to do the follwing settings.

