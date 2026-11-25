PhysicsLab3 scene snapshots

Enter a name in the Snapshot field, then click Save or Load.
Files are UTF-8 text in this directory: <name>.txt.
Missing files leave the current scene unchanged. Invalid files show a message
and also leave the scene unchanged. Saving the same name replaces that file.

W at the mouse position starts a wall preview. Move the mouse and press W
again to confirm the wall. Esc cancels. Only confirmed walls are saved.

Example format:

PHYSICSLAB3_SNAPSHOT 1
time_step 0.0166666675
mode CCD
paused true
playback_speed 1

object 0
position 100 120
size 32 32
mass 1
velocity 2000 0
force 0 0
visible true
end

Every object requires all six fields. Object numbers are unique labels in
the file. Comments start with #. Numbers use a decimal point.
position and size are in pixels; velocity is in pixels/second.
Direction is encoded by the signs of the velocity components.
mass 0 makes an object stationary; its velocity must then be 0 0.
To turn it into a moving object, set a positive mass and the desired velocity.
mode must be Primitive, CCD or MultyRate. time_step must be positive.
paused and visible accept true or false.
playback_speed is the playback multiplier, from 0.1 to 4; 1 is normal speed.
Old snapshots without this field load at 1x. It changes elapsed simulation
time, not time_step or body velocities. The Physics slider sets frequency
in Hz; the separate Playback slider controls slow motion or acceleration.

Snapshots include the boundary walls and all confirmed physical bodies.
Loading replaces them, rather than adding duplicates. Speed groups are
recomputed from mass and velocity. Mouse capture and unfinished wall
previews are not restored.

Measurements in the panel include physics milliseconds per render frame,
the latest physics-step time, base steps per frame, actual 60/120/240 group
substeps per frame, and CCD contacts. LIMIT means CCD exhausted its contact
budget and froze the remaining part of that step instead of tunneling.
