# Uoft_operating_systems
these 2 assignment should be run on Linux system.

### assignment 1 file system:

first run mkfs.c to format image file into a1fs file system, then run a1fs.c which implements read, truncate, write, mkdir and other command about file.
all code is done by myself except: __fs_ctx__, __map__, __options__. those are some helper functions.

### assignment 2 message queue:

this async message queue is implemented by mutex and conditional variable.
it provides one solution to multi-producer-consumer problem.
all code in msg_queue except __mq_init__, __mq_destroy__, __mq_open__, __mq_close__.