// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <common.h>
#include <bselftest.h>
#include <jsmn.h>

BSELFTEST_GLOBALS();

static const jsmntok_t*
__json_expect(const char *json, const jsmntok_t *token, const char **lookup,
	      jsmntype_t expected_type, const char *expected_value)
{
	bool success = true;

	total_tests++;

	if (token->type != expected_type) {
		failed_tests++;
		printf("%pJP: type mismatch: got %d, but %d expected\n",
		       lookup, token->type, expected_type);
		success = false;
	}

	if (!expected_value)
		goto out;

	total_tests++;

	if (!jsmn_eq(expected_value, json, token)) {
		failed_tests++;
		printf("%pJP: string mismatch: got %.*s, but %s expected\n",
		       lookup, (int)(token->end - token->start),
		       &json[token->start], expected_value);
		success = false;
	}

out:
	return success ? token : NULL;
}

static const jsmntok_t*
json_expect(const char *json, const jsmntok_t *tokens, const char **lookup,
	    jsmntype_t expected_type, const char *expected_value)
{
	const jsmntok_t *token;

	total_tests++;

	token = jsmn_locate(lookup, json, tokens);
	if (!token) {
		failed_tests++;
		printf("%pJP: couldn't be located\n", lookup);
		return NULL;
	}

	return __json_expect(json, token, lookup, expected_type, expected_value);
}

#define JP(...)	(const char *[]) { __VA_ARGS__, NULL }

/* Wonky indentation is intended */
static const char json[] =
"{\n"
"	\"null\" :null,\"number\" : 0x42,\n"
"	\"object\": {\n"
"		\"a\": \"b\",\n"
"		\"C\": \"dE\","
"		\"e\": \"\"\n"
"	},"
"	\"array\": [ \"1\",\"2\",\t\t\"3\"],\n"
"	\"boolean\": true,\n"
"\"string\": \"Hello World\n\"}\n";

static void test_json(void)
{
	ssize_t token_count;
	const jsmntok_t *token;
	jsmntok_t *tokens;
	jsmn_parser parser;
	char *string;
	int ret;

	total_tests++;

	jsmn_init(&parser);

	/* Figure out how many tokens we need. */
	ret = jsmn_parse(&parser, json, sizeof(json), NULL, 0);
	if (ret < 0) {
		printf("failed to determine number of tokens: %d\n", ret);
		failed_tests++;
		return;
	}

	token_count = ret;

	/* `token_count` is strictly less than `length` which is strictly less
	 * than CONFIG_SYS_EEPROM_SIZE (or 8K), so we should never overflow an
	 * int here.
	 */
	tokens = kmalloc_array(token_count, sizeof(jsmntok_t), GFP_KERNEL);
	if (WARN_ON(!tokens))
		return;

	total_tests++;

	jsmn_init(&parser);
	ret = jsmn_parse(&parser, json, sizeof(json), tokens, token_count);
	if (ret < 0) {
		printf("failed to parse JSON with tokens: %d\n", ret);
		failed_tests++;
		goto out;
	}

	json_expect(json, tokens, JP("null"), JSMN_PRIMITIVE, "null");
	json_expect(json, tokens, JP("number"), JSMN_PRIMITIVE, "0x42");
	json_expect(json, tokens, JP("object"), JSMN_OBJECT, NULL);
	json_expect(json, tokens, JP("object", "a"), JSMN_STRING, "b");
	json_expect(json, tokens, JP("object", "C"), JSMN_STRING, "dE");
	json_expect(json, tokens, JP("object", "e"), JSMN_STRING, "");

	token = json_expect(json, tokens, JP("array"), JSMN_ARRAY, NULL);

	token = jsmn_skip_value(token);
	__json_expect(json, token, JP("boolean"), JSMN_STRING, "boolean");

	token = jsmn_skip_value(token);
	__json_expect(json, token, JP("boolean"), JSMN_PRIMITIVE, "true");

	string = jsmn_strcpy(JP("string"), json, tokens);
	if (WARN_ON(!string))
		return;

	total_tests++;
	if (strcmp(string, "Hello World\n")) {
		failed_tests++;
		printf("%pJP: string mismatch\n", JP("string"));
	}

	free(string);
out:
	free(tokens);
}
bselftest(parser, test_json);
