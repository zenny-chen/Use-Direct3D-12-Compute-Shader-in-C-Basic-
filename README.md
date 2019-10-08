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

The demo project is built with Visual Studio 2019 community. Before you start, you may have to do the following stuffs.

1. Ensure that Visual Studio 2019 (Community) has been installed.
1. Ensure that Windows Kits for Windows 10 with DirectX 12 SDK have been installed. This can be installed with Visual Studio Installer.
1. Now, you can open the solution file (.sln) in this sample and check the directory in which DirectX 12 SDK was installed.
1. Next up, you should do the following configurations.

First, Move to the "**Project"** item in the menu bar and choose the current project's **properties** as following illustrated.

![1.jpg](https://github.com/zenny-chen/Use-Direct3D-12-Compute-Shader-in-C-Basic-/blob/master/1.JPG)

Second, expand **C/C++** item, substitue the **um** and **shared** *Include* directory in your own Windows 10 for the ones in this sample. The sample uses `C:\Program Files (x86)\Windows Kits\10\Include\10.0.18362.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.18362.0\shared`. Maybe the versions are different.

![2.jpg](https://github.com/zenny-chen/Use-Direct3D-12-Compute-Shader-in-C-Basic-/blob/master/2.JPG)

Last, expand **Linker**, substitute the **um\x64** *Lib* directory in your own Windows 10 for the one in this sample. The sample uses `C:\Program Files (x86)\Windows Kits\10\Lib\10.0.18362.0\um\x64`. Maybe the versions are different.

![3.jpg](https://github.com/zenny-chen/Use-Direct3D-12-Compute-Shader-in-C-Basic-/blob/master/3.JPG)

After all the work above, you can now run the sample successfully.
