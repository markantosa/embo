Things to note for loadcell data processing

Format 

example line of data : [490772] Motion > Test Both Motors: stroke 1 progress - left=14801 right=1199 - force1=-1730.63g force2=-0.40g

1. First 6 letter number [490772] is the timestamp of when the data was recorded. Due to HW limitations, exact time cannot be logged. 
2. left=14801 right=1199 is the position of the loadcell. Loadcell position is based on motor steps. When the loadcell is homed i.e. at the 
    top left and top right respectively, the motor step position is set as 0, as per the homing procedure. Max travel distance currently in 
    the firmware is 16000 motor steps (yet to calculate into physical distance).
3. force1=-1730.63g force2=-0.40g is the force/weight/mass measured by the loadcells. Left loadcell corresponds to force1, right loadcell corresponds to force2.

Naming of the files: 1s means 1 stroke, stroke is defined based on the old definition of 2 pumps. Due to some firmware issues, sometimes the logging crashed 
after a stroke was completed, hence data was collected in multiple files for 3s,5s and 9s. (for some reason it worked well during 7s) 9s1 means first file for 9s. 9s2,9s3... 
are the next files, arranged in order of which file was saved first i.e. 9s1 contains first few strokes, 9s7 contains last stroke data.

Hope this is clear enough
