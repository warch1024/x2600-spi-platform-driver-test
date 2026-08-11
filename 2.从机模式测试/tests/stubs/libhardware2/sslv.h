#ifndef TEST_SSLV_H
#define TEST_SSLV_H

struct sslv_config_data {
    unsigned int id;
    char *name;
    unsigned int bits_per_word;
    unsigned int sslv_pol;
    unsigned int sslv_pha;
    unsigned int loop_mode;
};

int sslv_open(char *sslv_dev_path);
void sslv_close(int fd);
int sslv_enable(int fd);
int sslv_disable(int fd);
int sslv_receive(int fd, void *buf, int size);
int sslv_send(int fd, void *buf, int size, char add_zero);
void sslv_get_info(int fd, struct sslv_config_data *info);
int sslv_set_mode(int fd, int mode);
int sslv_set_bits(int fd, int bits);

#endif
