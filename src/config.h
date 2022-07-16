#ifndef __W2G_CONFIG_H
#define __W2G_CONFIG_H

enum input_type {
    IN_TYPE_NONE,
    IN_TYPE_KEY_OR_BTN,
    IN_TYPE_REL,
    IN_TYPE_ABS
};

struct map_data {
    enum input_type intype;
    unsigned int input;
    int reversed; // bool
};

ssize_t read_config(const char *path);

#endif // __W2G_CONFIG_H