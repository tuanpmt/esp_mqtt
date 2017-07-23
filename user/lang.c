#include "c_types.h"
#include "mem.h"
#include "osapi.h"
#include "lang.h"
#include "user_config.h"
#include "mqtt_topics.h"
#include "ntp.h"

#include "easygpio.h"

#define lang_debug		//os_printf
#define lang_info 		//os_printf

extern uint8_t *my_script;
extern void do_command(char *t1, char *t2, char *t3);
extern void con_print(uint8_t *str);

static char EOT[] = "end of text";
#define len_check(x) \
if (interpreter_status==SYNTAX_CHECK && next_token+(x) >= max_token) \
  return syntax_error(next_token+(x), EOT)
#define syn_chk (interpreter_status==SYNTAX_CHECK)

#define ON "\xf0"
#define CONFIG "\xf1"

typedef struct _var_entry_t {
    uint8_t data[MAX_VAR_LEN];
    uint32_t data_len;
    Value_Type data_type;
} var_entry_t;

typedef struct _timestamp_entry_t {
    uint8_t *ts;
    bool happened;
} timestamp_entry_t;

char **my_token;
int max_token;
bool script_enabled = false;
bool in_topic_statement;
Interpreter_Status interpreter_status;
char *interpreter_topic;
char *interpreter_data;
int interpreter_data_len;
int interpreter_timer;
char *interpreter_timestamp;
int ts_counter;

static os_timer_t timers[MAX_TIMERS];
static var_entry_t vars[MAX_VARS];
static timestamp_entry_t timestamps[MAX_TIMESTAMPS];

static void ICACHE_FLASH_ATTR lang_timers_timeout(void *arg) {

    interpreter_timer = (int)arg;
    os_timer_disarm(&timers[interpreter_timer]);
    if (!script_enabled)
	return;

    lang_debug("timer %d expired\r\n", interpreter_timer + 1);

    interpreter_topic = interpreter_data = "";
    interpreter_data_len = 0;
    interpreter_status = TIMER;
    parse_statement(0);
}

void ICACHE_FLASH_ATTR init_timestamps(uint8_t * curr_time) {
    int i;

    for (i = 0; i < ts_counter; i++) {
	if (os_strcmp(curr_time, timestamps[i].ts) >= 0) {
	    timestamps[i].happened = true;
	} else {
	    timestamps[i].happened = false;
	}
    }
}

void ICACHE_FLASH_ATTR check_timestamps(uint8_t * curr_time) {
    int i;

    if (!script_enabled)
	return;
    for (i = 0; i < ts_counter; i++) {
	if (os_strcmp(curr_time, timestamps[i].ts) >= 0) {
	    if (timestamps[i].happened)
		continue;
	    lang_info("timerstamp %s happened\r\n", timestamps[i].ts);

	    interpreter_topic = interpreter_data = "";
	    interpreter_data_len = 0;
	    interpreter_status = CLOCK;
	    interpreter_timestamp = timestamps[i].ts;
	    parse_statement(0);
	    timestamps[i].happened = true;
	} else {
	    timestamps[i].happened = false;
	}
    }
}

void ICACHE_FLASH_ATTR test_tokens(void) {
    int i;

    for (i = 0; i < max_token; i++) {
	lang_debug("<%s>", my_token[i]);
    }
    lang_debug("\r\n");
}

int ICACHE_FLASH_ATTR text_into_tokens(char *str) {
    char *p, *q;
    int token_count = 0;
    bool in_token = false;

    // preprocessing
    lang_debug("lexxer preprocessing (prog_len: %d)\r\n", os_strlen(str));

    for (p = q = str; *p != 0; p++) {
	// special case "on" keyword - replace by special token ON (0xf0)
	if (!in_token && *p == 'o' && *(p + 1) == 'n' && *(p + 2) <= ' ') {
	    *q++ = '\xf0';
	    p += 1;
	    continue;
	}
	// special case "config" keyword - replace by special token CONFIG (0xf1)
	if (!in_token && *p == 'c' && *(p + 1) == 'o' && *(p + 2) == 'n'
	    && *(p + 3) == 'f' && *(p + 4) == 'i' && *(p + 5) == 'g' && *(p + 6) <= ' ') {
	    *q++ = '\xf1';
	    p += 5;
	    continue;
	}

	if (*p == '\\') {
	    // next char is quoted, copy that - skip this one
	    if (*(p + 1) != 0)
		*q++ = *++p;
	} else if (*p == '\"') {
	    // string quotation
	    in_token = !in_token;
	    *q++ = 1;
	} else if (*p == '%' && !in_token) {
	    // comment till eol
	    for (; *p != 0; p++)
		if (*p == '\n')
		    break;
	} else if (*p <= ' ' && !in_token) {
	    // mark this as whitespace
	    *q++ = 1;
	} else if (*p == '|' && !in_token) {
	    // mark this as concat
	    *q++ = 2;
	} else if (*p == '+' && !in_token) {
	    // mark this as add
	    *q++ = 3;
	} else if (*p == '-' && !in_token) {
	    // mark this as sub
	    *q++ = 4;
	} else if (*p == '*' && !in_token) {
	    // mark this as mult
	    *q++ = 5;
	} else if (*p == '=' && !in_token) {
	    // mark this as div
	    *q++ = 6;
	} else if (*p == '>' && !in_token) {
	    // mark this as div
	    *q++ = 7;
	} else {
	    *q++ = *p;
	}
    }
    *q = 0;

    // eliminate double whitespace, detect operators and count tokens
    lang_debug("lexxer whitespaces\r\n");

    in_token = false;
    for (p = q = str; *p != 0; p++) {
	if (*p < ' ') {
	    // it is a whitespace or an operator
	    if (*p == 1 && in_token) {
		// it is a whitespace
		*q++ = *p;
		in_token = false;
	    }
	    if (*p > 1) {
		// it is an operator
		*q++ = *p;
		token_count++;
		in_token = false;
	    }
	} else {
	    if (!in_token) {
		token_count++;
		in_token = true;
	    }
	    *q++ = *p;
	}
    }
    *q = 0;
    lang_debug("found %d tokens\r\n", token_count);

    // resize (shrink) the mem needed for the script
    lang_debug("prog_len compact: %d\r\n", (q-str)+1);
    my_script = (char *)os_realloc(my_script, (q-str)+5);
    str = &my_script[4];

    my_token = (char **)os_malloc(token_count * sizeof(char *));
    if (my_token == 0)
	return 0;

    // assign tokens
    lang_debug("lexxer tokenize\r\n");

    in_token = false;
    token_count = 0;
    for (p = str; *p != 0; p++) {
	if (*p == 1) {
	    *p = '\0';
	    in_token = false;
	} 
	else if (*p == 2) {
	    my_token[token_count++] = "|";
	    *p = '\0';
	    in_token = false;
	}
	else if (*p == 3) {
	    my_token[token_count++] = "+";
	    *p = '\0';
	    in_token = false;
	}
	else if (*p == 4) {
	    my_token[token_count++] = "-";
	    *p = '\0';
	    in_token = false;
	}
	else if (*p == 5) {
	    my_token[token_count++] = "*";
	    *p = '\0';
	    in_token = false;
	}
	else if (*p == 6) {
	    my_token[token_count++] = "=";
	    *p = '\0';
	    in_token = false;
	}
	else if (*p == 7) {
	    my_token[token_count++] = ">";
	    *p = '\0';
	    in_token = false;
	}
	else {
	    if (!in_token) {
		my_token[token_count++] = p;
		in_token = true;
	    }
	}
    }

    max_token = token_count;
    return max_token;
}

void ICACHE_FLASH_ATTR free_tokens(void) {
    if (my_token != NULL)
	os_free((uint32_t *) my_token);
    my_token = NULL;
}

bool ICACHE_FLASH_ATTR is_token(int i, char *s) {
    if (i >= max_token)
	return false;
//os_printf("cmp: %s %s\r\n", s, my_token[i]);
    return os_strcmp(my_token[i], s) == 0;
}

int ICACHE_FLASH_ATTR search_token(int i, char *s) {
    for (; i < max_token; i++)
	if (is_token(i, s))
	    return i;
    return max_token;
}

int ICACHE_FLASH_ATTR syntax_error(int i, char *message) {
    int j;

    os_sprintf(tmp_buffer, "Error (%s) at >>", message);
    for (j = i; j < i + 5 && j < max_token; j++) {
	int pos = os_strlen(tmp_buffer);
	if (is_token(j, ON))
	    my_token[j] = "on";
	if (is_token(j, CONFIG))
	    my_token[j] = "config";
	if (sizeof(tmp_buffer) - pos - 2 > os_strlen(my_token[j])) {
	    os_sprintf(tmp_buffer + pos, "%s ", my_token[j]);
	}
    }
    return -1;
}

int ICACHE_FLASH_ATTR parse_statement(int next_token) {
    bool event_happened;
    int on_token;

    uint32_t start = system_get_time();

    while ((next_token = syn_chk ? next_token : search_token(next_token, ON)) < max_token) {

	in_topic_statement = false;

	if (is_token(next_token, ON)) {
	    lang_debug("statement on\r\n");

	    if ((next_token = parse_event(next_token + 1, &event_happened)) == -1)
		return -1;
	    if (!syn_chk && !event_happened)
		continue;

	    if (syn_chk && !is_token(next_token, "do"))
		return syntax_error(next_token, "'do' expected");
	    if ((next_token = parse_action(next_token + 1, event_happened)) == -1)
		return -1;
	} else if (is_token(next_token, CONFIG)) {
	    next_token += 3;
	} else {
	    return syntax_error(next_token, "'on' or 'config' expected");
	}
    }

    lang_info("Interpreter loop: %d us\r\n", (system_get_time()-start));
    if (interpreter_status == INIT)
	loop_time = system_get_time()-start;
    else
	loop_time = (loop_time * 7 + (system_get_time()-start)) / 8;

    return next_token;
}

int ICACHE_FLASH_ATTR parse_event(int next_token, bool * happend) {
    *happend = false;

    if (is_token(next_token, "init")) {
	lang_debug("event init\r\n");

	*happend = (interpreter_status == INIT);
	return next_token + 1;
    }

    if (is_token(next_token, "mqttconnect")) {
	lang_debug("event mqttconnect\r\n");

	*happend = (interpreter_status == MQTT_CLIENT_CONNECT);
	return next_token + 1;
    }

    if (is_token(next_token, "topic")) {
	lang_debug("event topic\r\n");

	in_topic_statement = true;
	len_check(2);
	if (is_token(next_token + 1, "remote")) {
	    if (interpreter_status != TOPIC_REMOTE)
		return next_token + 3;
	} else if (is_token(next_token + 1, "local")) {
	    if (interpreter_status != TOPIC_LOCAL)
		return next_token + 3;
	} else {
	    return syntax_error(next_token + 1, "'local' or 'remote' expected");
	}

	*happend = Topics_matches(my_token[next_token + 2], true, interpreter_topic);

	if (*happend)
	    lang_info("topic %s %s %s match\r\n", my_token[next_token + 1],
		      my_token[next_token + 2], interpreter_topic);

	return next_token + 3;
    }

    if (is_token(next_token, "timer")) {
	lang_debug("event timer\r\n");

	len_check(1);
	uint32_t timer_no = atoi(my_token[next_token + 1]);
	if (timer_no == 0 || timer_no > MAX_TIMERS)
	    return syntax_error(next_token + 1, "invalid timer number");
	if (interpreter_status == TIMER && interpreter_timer == --timer_no) {
	    lang_info("timer %s expired\r\n", my_token[next_token + 1]);
	    *happend = true;
	}
	return next_token + 2;
    }

    else if (is_token(next_token, "clock")) {
	lang_debug("event clock\r\n");

	len_check(1);
	if (syn_chk && os_strlen(my_token[next_token + 1]) != 8)
	    return syntax_error(next_token, "invalid timestamp");
	if (syn_chk) {
	    if (ts_counter >= MAX_TIMESTAMPS)
		return syntax_error(next_token, "too many timestamps");
	    timestamps[ts_counter++].ts = my_token[next_token + 1];
	}
	*happend = (interpreter_status == CLOCK && os_strcmp(interpreter_timestamp, my_token[next_token + 1]) == 0);
	return next_token + 2;
    }

    return syntax_error(next_token, "'init', 'mqttconnect', 'topic', 'clock', or 'timer' expected");
}

int ICACHE_FLASH_ATTR parse_action(int next_token, bool doit) {

    while (next_token < max_token && !is_token(next_token, ON)
	   && !is_token(next_token, CONFIG) && !is_token(next_token, "endif")) {
	bool is_nl = false;

	if (doit) {
	    lang_debug("action %s\r\n", my_token[next_token]);
	}
	//os_printf("action %s %s\r\n", my_token[next_token], doit ? "do" : "ignore");

	if ((is_nl = is_token(next_token, "println")) || is_token(next_token, "print")) {
	    char *p_char;
	    int p_len;
	    Value_Type p_type;

	    len_check(1);
	    if ((next_token = parse_expression(next_token + 1, &p_char, &p_len, &p_type, doit)) == -1)
		return -1;
	    if (doit) {
		con_print(p_char);
		if (is_nl)
		    con_print("\r\n");
	    }
	}

	else if (is_token(next_token, "publish")) {
	    bool retained = false;
	    char *data;
	    int data_len;
	    Value_Type data_type;
	    char *topic;
	    int topic_len;
	    Value_Type topic_type;
	    int lr_token = next_token + 1;

	    len_check(3);
	    if ((next_token = parse_value(next_token + 2, &topic, &topic_len, &topic_type)) == -1)
		return -1;
	    if ((next_token = parse_value(next_token, &data, &data_len, &data_type)) == -1)
		return -1;
	    if (next_token < max_token && is_token(next_token, "retained")) {
		retained = true;
		next_token++;
	    }

	    if (doit) {
		if (data_type == STRING_T && data_len > 0)
		    data_len--;
		if (topic_type != STRING_T || Topics_hasWildcards(topic)) {
		    os_printf("invalid topic string\r\n");
		    return next_token;
		}
	    }

#ifdef MQTT_CLIENT
	    if (is_token(lr_token, "remote")) {
		if (doit && mqtt_connected) {
		    MQTT_Publish(&mqttClient, topic, data, data_len, 0, retained);
		    lang_info("published remote %s len: %d\r\n", topic, data_len);
		}
	    } else
#endif
	    if (is_token(lr_token, "local")) {
		if (doit) {
		    MQTT_local_publish(topic, data, data_len, 0, retained);
		    lang_info("published local %s len: %d\r\n", topic, data_len);
		}
	    } else {
		return syntax_error(lr_token, "'local' or 'remote' expected");
	    }
	}

	else if (is_token(next_token, "subscribe")) {
	    bool retval;

	    len_check(2);
#ifdef MQTT_CLIENT
	    if (is_token(next_token + 1, "remote")) {
		if (doit && mqtt_connected) {
		    retval = MQTT_Subscribe(&mqttClient, my_token[next_token + 2], 0);
		    lang_info("subsrcibe remote %s %s\r\n", my_token[next_token + 2], retval ? "success" : "failed");
		}
	    } else 
#endif
	    if (is_token(next_token + 1, "local")) {
		if (doit) {
		    retval = MQTT_local_subscribe(my_token[next_token + 2], 0);
		    lang_info("subsrcibe local %s %s\r\n", my_token[next_token + 2], retval ? "success" : "failed");
		}
	    } else {
		return syntax_error(next_token + 1, "'local' or 'remote' expected");
	    }
	    next_token += 3;
	}

	else if (is_token(next_token, "unsubscribe")) {
	    bool retval;

	    len_check(2);
#ifdef MQTT_CLIENT
	    if (is_token(next_token + 1, "remote")) {
		if (doit && mqtt_connected) {
		    retval = MQTT_UnSubscribe(&mqttClient, my_token[next_token + 2]);
		    lang_info("unsubsrcibe remote %s %s\r\n", my_token[next_token + 2], retval ? "success" : "failed");
		}
	    } else
#endif
	    if (is_token(next_token + 1, "local")) {
		if (doit) {
		    retval = MQTT_local_unsubscribe(my_token[next_token + 2]);
		    lang_info("unsubsrcibe local %s %s\r\n", my_token[next_token + 2], retval ? "success" : "failed");
		}
	    } else {
		return syntax_error(next_token + 1, "'local' or 'remote' expected");
	    }

	    next_token += 3;
	}

	else if (is_token(next_token, "if")) {
	    uint32_t if_val;
	    char *if_char;
	    int if_len;
	    Value_Type if_type;

	    len_check(3);
	    if ((next_token = parse_expression(next_token + 1, &if_char, &if_len, &if_type, doit)) == -1)
		return -1;
	    if (syn_chk && !is_token(next_token, "then"))
		return syntax_error(next_token, "'then' expected");

	    if (doit) {
		if_val = atoi(if_char);
		lang_info("if %s\r\n", if_val != 0 ? "done" : "not done");
	    }
	    if ((next_token = parse_action(next_token + 1, doit && if_val != 0)) == -1)
		return -1;
	}

	else if (is_token(next_token, "settimer")) {
	    len_check(2);
	    uint32_t timer_no = atoi(my_token[next_token + 1]);
	    if (timer_no == 0 || timer_no > MAX_TIMERS)
		return syntax_error(next_token + 1, "invalid timer number");

	    uint32_t timer_val;
	    char *timer_char;
	    int timer_len;
	    Value_Type timer_type;
	    if ((next_token = parse_value(next_token + 2, &timer_char, &timer_len, &timer_type)) == -1)
		return -1;

	    if (doit) {
		timer_val = atoi(timer_char);
		lang_info("settimer %d %d\r\n", timer_no, timer_val);
		timer_no--;

		os_timer_disarm(&timers[timer_no]);
		if (timer_val != 0) {
		    os_timer_setfn(&timers[timer_no], (os_timer_func_t *) lang_timers_timeout, timer_no);
		    os_timer_arm(&timers[timer_no], timer_val, 0);
		}
	    }
	}

	else if (is_token(next_token, "setvar")) {
	    len_check(3);
	    if (syn_chk && my_token[next_token + 1][0] != '$')
		return syntax_error(next_token + 1, "invalid var identifier");
	    uint32_t var_no = atoi(&(my_token[next_token + 1][1]));
	    if (var_no == 0 || var_no > MAX_VARS)
		return syntax_error(next_token + 1, "invalid var number");
	    if (syn_chk && os_strcmp(my_token[next_token + 2], "=") != 0)
		return syntax_error(next_token + 2, "'=' expected");

	    char *var_data;
	    int var_len;
	    Value_Type var_type;
	    if ((next_token = parse_expression(next_token + 3, &var_data, &var_len, &var_type, doit)) == -1)
		return -1;

	    if (doit) {
		lang_info("setvar $%d \r\n", var_no);
		if (var_len > MAX_VAR_LEN) {
		    os_printf("Var $%d too long '%s'\r\n", var_no, var_data);
		    return next_token;
		}
		var_no--;
		os_memcpy(vars[var_no].data, var_data, var_len);
		vars[var_no].data_len = var_len;
		vars[var_no].data_type = var_type;
	    }
	}
#ifdef GPIO
	else if (is_token(next_token, "gpio_out")) {
	    len_check(2);

	    uint32_t gpio_no = atoi(my_token[next_token + 1]);
	    if (gpio_no > 16)
		return syntax_error(next_token + 1, "invalid gpio number");

	    char *gpio_data;
	    int gpio_len;
	    Value_Type gpio_type;
	    if ((next_token = parse_expression(next_token + 2, &gpio_data, &gpio_len, &gpio_type, doit)) == -1)
		return -1;

	    if (doit) {
		lang_info("gpio_out %d %d\r\n", gpio_no, atoi(gpio_data) != 0);

		if (easygpio_pinMode(gpio_no, EASYGPIO_NOPULL, EASYGPIO_OUTPUT))
		    easygpio_outputSet(gpio_no, atoi(gpio_data) != 0);
	    }
	}
#endif
	else
	    return syntax_error(next_token, "action command expected");

    }
    if (is_token(next_token, "endif"))
	next_token++;
    return next_token;
}

int ICACHE_FLASH_ATTR parse_expression(int next_token, char **data, int *data_len, Value_Type * data_type, bool doit) {

    if (is_token(next_token, "not")) {
	len_check(1);
	lang_debug("expr not\r\n");

	if ((next_token = parse_expression(next_token + 1, data, data_len, data_type, doit)) == -1)
	    return -1;
	*data = atoi(*data) ? "0" : "1";
	*data_len = 1;
	*data_type = STRING_T;
    }

    else {
	if ((next_token = parse_value(next_token, data, data_len, data_type)) == -1)
	    return -1;

	// if it is not some kind of binary operation - finished
	if (!is_token(next_token, "=") 
	    && !is_token(next_token, "+")
	    && !is_token(next_token, "-")
	    && !is_token(next_token, "*")
	    && !is_token(next_token, "div")
	    && !is_token(next_token, "|")
	    && !is_token(next_token, ">")
	    && !is_token(next_token, "gte")
	    && !is_token(next_token, "str_gt")
	    && !is_token(next_token, "str_gte"))
	    return next_token;

	// okay, it is an operation
	int op = next_token;

	char *r_data;
	int r_data_len;
	Value_Type r_data_type;

	// parse second operand
	if ((next_token = parse_expression(next_token + 1, &r_data, &r_data_len, &r_data_type, doit)) == -1)
	    return -1;
	//os_printf("l:%s(%d) r:%s(%d)\r\n", *data, *data_len, r_data, r_data_len);

	if (!doit)
	    return next_token;

	*data_type = STRING_T;
	if (is_token(op, "=")) {
	    *data = os_strcmp(*data, r_data) ? "0" : "1";
	} else if (is_token(op, "+")) {
	    os_sprintf(tmp_buffer, "%d", atoi(*data) + atoi(r_data));
	    *data = tmp_buffer;
	    *data_len = os_strlen(tmp_buffer);
	} else if (is_token(op, "-")) {
	    os_sprintf(tmp_buffer, "%d", atoi(*data) - atoi(r_data));
	    *data = tmp_buffer;
	    *data_len = os_strlen(tmp_buffer);
	} else if (is_token(op, "*")) {
	    os_sprintf(tmp_buffer, "%d", atoi(*data) * atoi(r_data));
	    *data = tmp_buffer;
	    *data_len = os_strlen(tmp_buffer);
	} else if (is_token(op, "div")) {
	    os_sprintf(tmp_buffer, "%d", atoi(*data) / atoi(r_data));
	    *data = tmp_buffer;
	    *data_len = os_strlen(tmp_buffer);
	} else if (is_token(op, "|")) {
	    uint16_t len = os_strlen(*data) + os_strlen(r_data);
	    char catbuf[len+1];
	    os_sprintf(catbuf, "%s%s", *data, r_data);
	    if (len > sizeof(tmp_buffer)-1) {
		len = sizeof(tmp_buffer);
		catbuf[len] = '\0';
	    }
	    *data_len = len;
	    os_memcpy(tmp_buffer, catbuf, *data_len + 1);
	    *data = tmp_buffer;
	} else if (is_token(op, ">")) {
	    *data = atoi(*data) > atoi(r_data) ? "1" : "0";
	    *data_len = 1;
	} else if (is_token(op, "gte")) {
	    *data = atoi(*data) >= atoi(r_data) ? "1" : "0";
	    *data_len = 1;
	} else if (is_token(op, "str_gt")) {
	    *data = os_strcmp(*data, r_data) > 0 ? "1" : "0";
	    *data_len = 1;
	} else if (is_token(op, "str_gte")) {
	    *data = os_strcmp(*data, r_data) >= 0 ? "1" : "0";
	    *data_len = 1;
	}
    }

    return next_token;
}

int ICACHE_FLASH_ATTR parse_value(int next_token, char **data, int *data_len, Value_Type * data_type) {
    if (is_token(next_token, "$this_data")) {
	lang_debug("val $this_data\r\n");
	if (!in_topic_statement)
	    return syntax_error(next_token, "undefined $this_data");
	*data = interpreter_data;
	*data_len = interpreter_data_len;
	*data_type = DATA_T;
	return next_token + 1;
    }

    else if (is_token(next_token, "$this_topic")) {
	lang_debug("val $this_topic\r\n");
	if (!in_topic_statement)
	    return syntax_error(next_token, "undefined $this_topic");
	*data = interpreter_topic;
	*data_len = os_strlen(interpreter_topic) + 1;
	*data_type = STRING_T;
	return next_token + 1;
    }
#ifdef NTP
    else if (is_token(next_token, "$timestamp")) {
	lang_debug("val $timestamp\r\n");
	if (ntp_sync_done())
	    *data = get_timestr();
	else
	    *data = "99:99:99";
	*data_len = os_strlen(*data) + 1;
	*data_type = STRING_T;
	return next_token + 1;
    }
#endif

/*   else if (my_token[next_token][0] == '\'') {
	char *p = &(my_token[next_token][1]);
	int *b = (int*)&val_buf[0];

	*b = atoi(htonl(p));
	*data = val_buf;
	*data_len = sizeof(int);
	return next_token+1;
   }
*/
    else if (my_token[next_token][0] == '$' && my_token[next_token][1] <= '9') {
	uint32_t var_no = atoi(&(my_token[next_token][1]));
	if (var_no == 0 || var_no > MAX_VARS)
	    return syntax_error(next_token + 1, "invalid var number");
	var_no--;

	*data = vars[var_no].data;
	*data_len = vars[var_no].data_len;
	*data_type = vars[var_no].data_type;
	return next_token + 1;
    }

    else if (my_token[next_token][0] == '#') {

	lang_debug("val hexbinary\r\n");

	// Convert it in place to binary once during syntax check
	// Format: Byte 0: '#', Byte 1: len (max 255), Byte 2 to len: binary data, Byte len+1: 0
	if (syn_chk) {
	    int i, j, len = os_strlen(my_token[next_token]);
	    uint8_t a, *p = &(my_token[next_token][1]);

	    if (len < 3)
		return syntax_error(next_token, "hexbinary too short");
	    if (len > 511)
		return syntax_error(next_token, "hexbinary too long");
	    for (i = 0, j = 1; i < len - 1; i += 2, j++) {
		if (p[i] <= '9')
		    a = p[i] - '0';
		else
		    a = toupper(p[i]) - 'A' + 10;
		a <<= 4;
		if (p[i + 1] <= '9')
		    a += p[i + 1] - '0';
		else
		    a += toupper(p[i + 1]) - 'A' + 10;
		p[j] = a;
	    }
	    p[j] = '\0';
	    p[0] = (uint8_t) j - 2;
	}

	*data = &my_token[next_token][2];
	*data_len = my_token[next_token][1];
	*data_type = DATA_T;
	return next_token + 1;
    }

    else {
	*data = my_token[next_token];
	*data_len = os_strlen(my_token[next_token]) + 1;
	*data_type = STRING_T;
	return next_token + 1;
    }
}

int ICACHE_FLASH_ATTR interpreter_syntax_check() {
    lang_debug("interpreter_syntax_check\r\n");

    os_sprintf(tmp_buffer, "Syntax okay");
    interpreter_status = SYNTAX_CHECK;
    interpreter_topic = interpreter_data = "";
    interpreter_data_len = 0;
    os_bzero(&timestamps, sizeof(timestamps));
    ts_counter = 0;
    return parse_statement(0);
}

int ICACHE_FLASH_ATTR interpreter_config() {
    int next_token = 0;

    while ((next_token = search_token(next_token, CONFIG)) < max_token) {
	lang_debug("statement config\r\n");

	len_check(2);
	do_command("set", my_token[next_token + 1], my_token[next_token + 2]);
	next_token += 3;
    }
    return next_token;
}

int ICACHE_FLASH_ATTR interpreter_init() {
    if (!script_enabled)
	return -1;

    lang_debug("interpreter_init\r\n");

    interpreter_status = INIT;
    interpreter_topic = interpreter_data = "";
    interpreter_data_len = 0;
    return parse_statement(0);
}

int ICACHE_FLASH_ATTR interpreter_reconnect(void) {
    if (!script_enabled)
	return -1;

    lang_debug("interpreter_init_reconnect\r\n");

    interpreter_status = MQTT_CLIENT_CONNECT;
    interpreter_topic = interpreter_data = "";
    interpreter_data_len = 0;
    return parse_statement(0);
}

int ICACHE_FLASH_ATTR interpreter_topic_received(const char *topic, const char *data, int data_len, bool local) {
    if (!script_enabled)
	return -1;

    lang_debug("interpreter_topic_received\r\n");

    interpreter_status = (local) ? TOPIC_LOCAL : TOPIC_REMOTE;
    interpreter_topic = (char *)topic;
    interpreter_data_len = data_len;
    if ((interpreter_data = (uint8_t *) os_malloc(data_len + 1)) == 0) {
	os_printf("Out of memory\r\n");
	return -1;
    }
    os_memcpy(interpreter_data, data, data_len);
    interpreter_data[data_len] = '\0';

    int retval = parse_statement(0);

    os_free(interpreter_data);
    return retval;
}
