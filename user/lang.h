#include "mqtt_server.h"

extern MQTT_Client mqttClient;
extern bool mqtt_enabled, mqtt_connected;
uint8_t tmp_buffer[128];
uint32_t loop_time;

typedef enum {SYNTAX_CHECK, CONFIG, INIT, MQTT_CLIENT_CONNECT, TOPIC_LOCAL, TOPIC_REMOTE, TIMER, GPIO_INT, CLOCK} Interpreter_Status;
typedef enum {STRING_T, DATA_T} Value_Type;

int text_into_tokens(char *str);
void free_tokens(void);
bool is_token(int i, char *s);
int search_token(int i, char *s);
int syntax_error(int i, char *message);

int parse_statement(int next_token);
int parse_event(int next_token, bool *happened);
int parse_action(int next_token, bool doit);
int parse_expression(int next_token, char **data, int *data_len, Value_Type * data_type, bool doit);
int parse_value(int next_token, char **data, int *data_len, Value_Type *data_type);

extern bool script_enabled;
int interpreter_syntax_check();
int interpreter_config();
int interpreter_init();
int interpreter_reconnect(void);
int interpreter_topic_received(const char *topic, const char *data, int data_len, bool local);

void init_timestamps(uint8_t *curr_time);
void check_timestamps(uint8_t *curr_time);

void init_gpios();
void stop_gpios();
