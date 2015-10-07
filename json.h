#ifndef JSON_H
#define JSON_H

#include <stddef.h>

typedef struct js_member js_member;
typedef struct js_struct js_struct;

typedef enum
{
    TYPE_INT = 0,           /* a signed C integer */
    TYPE_STRING = 1,        /* a pointer to a null-terminated string */
    TYPE_OBJ = 2,           /* a pointer to an object */
    TYPE_OBJ_ARRAY = 3      /* a pointer to a block of objects */
}
JS_TYPE;

struct js_member
{
    JS_TYPE type;           /* data type of field */
    const char* name;       /* name of field */
    size_t offset;          /* offset in struct */
    const js_struct* desc;  /* further description of object types */
    size_t sizeoffset;      /* offset of array count if array type */
};

struct js_struct
{
    const js_member* members;
    size_t num_members;
    size_t objsize;         /* sizeof the C-struct */
};



/* get the description of a structure */
#define JSON_DESC( container ) (container##_desc)

/* get the array of member field descriptions of a structure */
#define JSON_FIELDS( container ) (container##_members)

/* get the number of recorded member fields inside a structure */
#define JSON_NUM_FIELDS( container )\
        (sizeof(JSON_FIELDS(container))/sizeof(JSON_FIELDS(container)[0]))


/* start recording reflection data on a structure */
#define JSON_BEGIN( container )\
        static const js_member JSON_FIELDS( container )[] = {

/* record reflection data for an int inside a structure */
#define JSON_INT( container, name )\
        { TYPE_INT, #name, offsetof(container,name), NULL, 0 }

/* record reflection data for a string inside a structure */
#define JSON_STRING( container, name )\
        { TYPE_STRING, #name, offsetof(container,name), NULL, 0 }

/* record reflection data for an object pointer inside a structure */
#define JSON_OBJ( container, name, subname )\
        { TYPE_OBJ, #name, offsetof(container,name), &JSON_DESC(subname), 0 }

/* record reflection data for an object array inside a structure */
#define JSON_ARRAY( container, name, subname )\
        { TYPE_OBJ_ARRAY, #name, offsetof(container,name),\
          &JSON_DESC(subname), offsetof(container,num_##name) }

/* finish recoding reflection data on a structure */
#define JSON_END( container )\
        };\
        static const js_struct JSON_DESC( container ) = {\
            JSON_FIELDS( container ),\
            JSON_NUM_FIELDS( container ),\
            sizeof( container )\
        }


/*
    call json_free on all object pointers in an object, call json_free_array
    on all array pointers and free all string objects and arrays in an object.
 */
void json_free( void* obj, const js_struct* desc );

/*
    call json_free on all elements in an array
 */
void json_free_array( void* arr, size_t count, const js_struct* desc );

/*
    Parse a json string and generate an object. Returns the number of
    characters consumed (0 on failure).
 */
size_t json_parse( void* obj, const js_struct* desc, const char* str );

/*
    Parse a json array and return a pointer to the generated array.
    Returns the number of characters consumed (0 on failure).
 */
size_t json_parse_array( void** out, size_t* count,
                         const js_struct* desc, const char* str );

#endif /* JSON_H */

