#ifndef _S_I_HELLO_QUEUE_H
#define _S_I_HELLO_QUEUE_H

/* MSG_SIZE will be different in tests. */
#define MSG_SIZE 7

#define HQIOCRES  _IO('a', 1)
#define HQIOCSET  _IOW('a', 2, char[MSG_SIZE])
#define HQIOCXCH  _IOW('a', 3, char[2])
#define HQIOCDEL  _IO('a', 4)

#endif /* _S_I_HELLO_QUEUE_H */
