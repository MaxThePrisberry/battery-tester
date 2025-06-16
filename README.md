# Battery Tester
This git project is created solely for the purpose of version control. The bare upstream repository is located at `/srv/batter-tester.git/`.

# Setup

#### Biologic SP-150e
The DLL files used to connect to the SP-150e device are included as part of the Lab Development Package. The lib directory of the development package is included as part of the project in the `/lib` folder. In order for LabWindows/CVI to find the DLLs when it dynamically loads them they need to either be in the root directory of the project (which they are not) or part of the PATH (which they are). If the location of the project root directory changes, this needs to be accounted for. It is currently unknown what other files in the direcotory need to be accessed through the PATH variable besides the DLLs themselves.
