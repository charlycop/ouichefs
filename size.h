#ifndef _SIZE_H
#define _SIZE_H

typedef enum TypePolicy TypePolicy;
enum TypePolicy {biggest, oldest, directory, partition};

typedef struct policy
{
    TypePolicy val;

}policies;

#endif	/* _SIZE_H */
