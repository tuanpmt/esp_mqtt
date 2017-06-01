/*******************************************************************************
 * Copyright (c) 2007, 2013 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *******************************************************************************/

/**
 * @file
 * Topic handling functions.
 *
 * Topic syntax matches that of other MQTT servers such as Micro broker.
 */

#include "mqtt_topics.h"

#include "os_type.h"
#include "osapi.h"
#include "mem.h"

char *_strtok_r(char *s, const char *delim, char **last);

char *_strchr(const char *s, int c)
{
    while (*s != (char)c)
        if (!*s++)
            return 0;
    return (char *)s;
}

char *_strdup(char *src)
{
    char *str;
    char *p;
    int len = 0;

    while (src[len])
        len++;
    str = (char*)os_malloc(len + 1);
    p = str;
    while (*src)
        *p++ = *src++;
    *p = '\0';
    return str;
}


/**
 * Checks that the syntax of a topic string is correct.
 * @param aName the topic name string
 * @return boolean value indicating whether the topic name is valid
 */
int Topics_isValidName(char* aName)
{
	int rc = true;
	char *c = NULL;
	int length = os_strlen(aName);
	char* hashpos = _strchr(aName, '#'); /* '#' wildcard can be only at the beginning or the end of a topic */

	if (hashpos != NULL)
	{
		char* second = _strchr(hashpos+1, '#');
		if ((hashpos != aName && hashpos != aName+(length-1)) || second != NULL)
			rc = false;
	}

	/* '#' or '+' only next to a slash separator or end of name */
	for (c = "#+"; *c != '\0'; ++c)
	{
		char* pos = _strchr(aName, *c);
		while (pos != NULL)
		{
			if (pos > aName)                  /* check previous char is '/'*/
			{
				if (*(pos - 1) != '/')
					rc = false;
			}
			if (*(pos + 1) != '\0')      /* check that subsequent char is '/'*/
			{
				if (*(pos + 1) != '/')
					rc = false;
			}
			pos = _strchr(pos + 1, *c);
		}
	}

	return rc;
}


/**
 * Reverse a string.
 * Linux utility function for Linux to enable Windows/Linux portability
 * @param astr the character string to reverse
 * @return pointer to the reversed string which was reversed in place
 */
char* _strrev(char* astr)
{
	char* forwards = astr;
	int len = os_strlen(astr);
	if (len > 1)
	{
		char* backwards = astr + len - 1;
		while (forwards < backwards)
		{
			char temp = *forwards;
			*forwards++ = *backwards;
			*backwards-- = temp;
		}
	}
	return astr;
}


/**
 * Does a topic string contain wildcards?
 * @param topic the topic name string
 * @return boolean value indicating whether the topic contains a wildcard or not
 */
int Topics_hasWildcards(char* topic)
{
	return (_strchr(topic, '+') != NULL) || (_strchr(topic, '#') != NULL);
}


/**
 * Tests whether one topic string matches another where one can contain wildcards.
 * @param wildTopic a topic name string that can contain wildcards
 * @param topic a topic name string that must not contain wildcards
 * @return boolean value indicating whether topic matches wildTopic
 */
int Topics_matches(char* wildTopic, int wildcards, char* topic)
{
	int rc = false;
	char *last1 = NULL, *last2 = NULL;
	char *pwild = NULL, *pmatch = NULL;

	if (!wildcards)
	{
		rc = (os_strcmp(wildTopic, topic) == 0);
		goto exit;
	}

	if (Topics_hasWildcards(topic))
	{
		//os_printf("Topics_matches: should not be wildcard in topic %s", topic);
		goto exit;
	}
	if (!Topics_isValidName(wildTopic))
	{
		//os_printf("Topics_matches: invalid topic name %s", wildTopic);
		goto exit;
	}
	if (!Topics_isValidName(topic))
	{
		//os_printf("Topics_matches: invalid topic name %s", topic);
		goto exit;
	}

	if (strcmp(wildTopic, MULTI_LEVEL_WILDCARD) == 0 || /* Hash matches anything... */
		strcmp(wildTopic, topic) == 0)
	{
		rc = true;
		goto exit;
	}

	if (strcmp(wildTopic, "/#") == 0)  /* Special case for /# matches anything starting with / */
	{
		rc = (topic[0] == '/') ? true : false;
		goto exit;
	}

	/* because strtok will return bill when matching /bill/ or bill in a topic name for the first time,
		 * we have to check whether the first character is / explicitly.
		 */
	if ((wildTopic[0] == TOPIC_LEVEL_SEPARATOR[0]) && (topic[0] != TOPIC_LEVEL_SEPARATOR[0]))
			goto exit;

	if ((wildTopic[0] == SINGLE_LEVEL_WILDCARD[0]) && (topic[0] == TOPIC_LEVEL_SEPARATOR[0]))
			goto exit;

	/* We only match hash-first topics in reverse, for speed */
	if (wildTopic[0] == MULTI_LEVEL_WILDCARD[0])
	{
		wildTopic = (char*)_strrev(_strdup(wildTopic));
		topic = (char*)_strrev(_strdup(topic));
	}
	else
	{
		wildTopic = (char*)_strdup(wildTopic);
		topic = (char*)_strdup(topic);
	}

	pwild = _strtok_r(wildTopic, TOPIC_LEVEL_SEPARATOR, &last1);
	pmatch = _strtok_r(topic, TOPIC_LEVEL_SEPARATOR, &last2);

	/* Step through the subscription, level by level */
	while (pwild != NULL)
	{
		/* Have we got # - if so, it matches anything. */
		if (strcmp(pwild, MULTI_LEVEL_WILDCARD) == 0)
		{
			rc = true;
			break;
		}
		/* Nope - check for matches... */
		if (pmatch != NULL)
		{
			if (strcmp(pwild, SINGLE_LEVEL_WILDCARD) != 0 && strcmp(pwild, pmatch) != 0)
				/* The two levels simply don't match... */
				break;
		}
		else
			break; /* No more tokens to match against further tokens in the wildcard stream... */
		pwild = _strtok_r(NULL, TOPIC_LEVEL_SEPARATOR, &last1);
		pmatch = _strtok_r(NULL, TOPIC_LEVEL_SEPARATOR, &last2);
	}

	/* All tokens up to here matched, and we didn't end in #. If there
		are any topic tokens remaining, the match is bad, otherwise it was
		a good match. */
	if (pmatch == NULL && pwild == NULL)
		rc = true;

	/* Now free the memory allocated in strdup() */
	os_free(wildTopic);
	os_free(topic);
exit:
	return rc;
}                                                            /* end matches*/

#ifdef UNIT_TEST
#if !defined(ARRAY_SIZE)
/**
 * Macro to calculate the number of entries in an array
 */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

int test()
{
	int i;

	struct
	{
		char* str;
	} tests0[] = {
		"#", "jj",
		"+/a", "adkj/a",
		"+/a", "adsjk/adakjd/a", "a/+", "a/#", "#/a"
	};

	for (i = 0; i < sizeof(tests0)/sizeof(char*); ++i)
	{
		os_printf("topic %s, isValidName %d\n", tests0[i].str, Topics_isValidName(tests0[i].str));
		//assert(Topics_isValidName(tests0[i].str) == 1);
	}

	struct
	{
		char* wild;
		char* topic;
		int result;
	} tests1[] = {
		{ "#", "jj" , 1},
		{ "+/a", "adkj/a", 1},
		{ "+/a", "adsjk/adakjd/a", 0},
		{ "+/+/a", "adsjk/adakjd/a", 1},
		{ "#/a", "adsjk/adakjd/a", 1},
		{ "test/#", "test/1", 1},
		{ "test/+", "test/1", 1},
		{ "+", "test1", 1},
		{ "+", "test1/k", 0},
		{ "+", "/test1/k", 0},
		{ "/+", "test1/k", 0},
		{ "+", "/jkj", 0},
		{ "/+", "/test1", 1},
		{ "+/+", "/test1", 0},
		{ "+/+", "test1/k", 1},
		{ "/#", "/test1/k", 1},
		{ "/#", "test1/k", 0},
	};

	for (i = 0; i < ARRAY_SIZE(tests1); ++i)
	{
		os_printf("wild: %s, topic %s, result %d %d (should)\n", tests1[i].wild, tests1[i].topic,
			Topics_matches(_strdup(tests1[i].wild), 1, _strdup(tests1[i].topic)), tests1[i].result);
		//assert(Topics_matches(_strdup(tests1[i].wild), _strdup(tests1[i].topic)) == tests1[i].result);
	}

	struct
	{
		char* str;
		char* result;
	} tests2[] = {
		{ "#", "#" },
		{ "ab", "ba" },
		{ "abc", "cba" },
		{ "abcd", "dcba" },
		{ "abcde", "edcba" }
	};
	for (i = 0; i < 5; ++i)
	{
		os_printf("str: %s, _strrev %s\n", tests2[i].str, _strrev(_strdup(tests2[i].str)));
		//assert(strcmp(tests2[i].result, _strrev(_strdup(tests2[i].str))) == 0);
	}
}

#endif
