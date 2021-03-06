/**
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "escalate_module.h"
#include "escalate_test.h"
#include "test.h"

#include <security/pam_appl.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int expect_style;
  gchar *expect_message;
  gchar *return_response;
  gboolean return_error;
} MockConversationMessage;

typedef struct {
  const MockConversationMessage *messages;
  guint length;
  guint consumed;
} MockConversation;


static MockConversationMessage mock_auth_success_messages [] = {
  { PAM_PROMPT_ECHO_OFF, "Password: ", "testpass" },
  { PAM_TEXT_INFO, "Success!", NULL },
};

static gchar *mock_auth_success_expect [] = {
  "(1, <(1, 0, 'janedoe', {3: @ms '/dev/pts/9000'}, {'PATH': '/path'})>)",
  "(3, <(@ms 'testpass', 0)>)",
  "(3, <(@ms nothing, 0)>)",
  NULL,
};

static gchar *mock_auth_success_respond [] = {
  "(2, <(1, 'Password: ')>)",
  "(2, <(4, 'Success!')>)",
  "(4, <(0, {'PATH': '/newpath'})>)",
  NULL,
};

static gchar *mock_auth_system_err_respond [] = {
  "(2, <(1, 'Password: ')>)",
  "(2, <(4, 'Success!')>)",
  NULL,
};

static MockConversationMessage mock_auth_err_messages [] = {
  { PAM_PROMPT_ECHO_OFF, "Password: ", "badpass" },
  { PAM_ERROR_MSG, "Failed!", NULL },
};

static gchar *mock_auth_err_expect [] = {
  "(1, <(1, 0, 'janedoe', {3: @ms '/dev/pts/9001'}, {'PATH': '/path'})>)",
  "(3, <(@ms 'badpass', 0)>)",
  "(3, <(@ms nothing, 0)>)",
  NULL,
};

static gchar *mock_auth_err_respond [] = {
  "(2, <(1, 'Password: ')>)",
  "(2, <(3, 'Failed!')>)",
  "(4, <(7, @a{ss} {})>)",
  NULL,
};

static MockConversationMessage mock_auth_null_messages [] = {};
static gchar *mock_auth_null_expect [] = { NULL };
static gchar *mock_auth_null_respond [] = { NULL };


#define MockConversationNew(messages) \
    _MockConversationNew(messages, G_N_ELEMENTS(messages))


static MockConversation *_MockConversationNew(
    const MockConversationMessage *messages,
    guint length) {
  MockConversation *result = g_new0(MockConversation, 1);
  result->length = length;
  result->messages = messages;
  return result;
}


static void MockConversationAssertFinished(struct pam_conv *conversation) {
  MockConversation *self = (MockConversation *) conversation->appdata_ptr;
  g_assert_cmpint(self->length, ==, self->consumed);
  g_free(self);
}


static int MockConversationCallback(gint num_messages,
                                    const struct pam_message **messages,
                                    struct pam_response **responses,
                                    void *ctx) {
  MockConversation *self = (MockConversation *) ctx;
  const MockConversationMessage *mock_message = NULL;
  const struct pam_message *message = NULL;
  struct pam_response *response = NULL;

  g_assert_cmpint(1, ==, num_messages);
  g_assert_cmpint(self->consumed, <, self->length);

  mock_message = &self->messages[self->consumed];
  message = messages[0];
  self->consumed++;

  g_assert_cmpstr(mock_message->expect_message, ==, message->msg);
  g_assert_cmpint(mock_message->expect_style, ==, message->msg_style);

  if (mock_message->return_error)
    return PAM_CONV_ERR;

  response = malloc(sizeof(*response));
  response->resp = NULL;
  response->resp_retcode = 0;
  if (mock_message->return_response)
    response->resp = strdup(mock_message->return_response);
  *responses = response;
  return PAM_SUCCESS;
}


void TestAuthSuccess() {
  pam_handle_t *handle = NULL;
  int status = PAM_SYSTEM_ERR;
  struct pam_conv conversation = {
    MockConversationCallback, MockConversationNew(mock_auth_success_messages) };
  EscalateTestSetMockHelperMessages(mock_auth_success_expect,
                                    mock_auth_success_respond);

  status = pam_start("mockservice", "janedoe", &conversation, &handle);
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  status = pam_set_item(handle, PAM_TTY, "/dev/pts/9000");
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  status = pam_putenv(handle, "PATH=/path");
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  status = EscalateModuleMain(ESCALATE_MESSAGE_ACTION_AUTHENTICATE, handle, 0,
                              0, NULL);
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  MockConversationAssertFinished(&conversation);
  g_assert_cmpstr("/newpath", ==, pam_getenv(handle, "PATH"));
  pam_end(handle, PAM_SUCCESS);
}


void TestAuthErr() {
  pam_handle_t *handle = NULL;
  int status = PAM_SYSTEM_ERR;
  struct pam_conv conversation = {
    MockConversationCallback, MockConversationNew(mock_auth_err_messages) };
  EscalateTestSetMockHelperMessages(mock_auth_err_expect,
                                    mock_auth_err_respond);

  status = pam_start("mockservice", "janedoe", &conversation, &handle);
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  status = pam_set_item(handle, PAM_TTY, "/dev/pts/9001");
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  status = pam_putenv(handle, "PATH=/path");
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  status = EscalateModuleMain(ESCALATE_MESSAGE_ACTION_AUTHENTICATE, handle, 0,
                              0, NULL);
  g_assert_cmpint(PAM_AUTH_ERR, ==, status);

  MockConversationAssertFinished(&conversation);
  pam_end(handle, PAM_AUTH_ERR);
}


void TestAuthSystemErr() {
  pam_handle_t *handle = NULL;
  int status = PAM_SYSTEM_ERR;
  struct pam_conv conversation = {
    MockConversationCallback, MockConversationNew(mock_auth_success_messages) };
  EscalateTestSetMockHelperMessages(mock_auth_success_expect,
                                    mock_auth_system_err_respond);

  status = pam_start("mockservice", "janedoe", &conversation, &handle);
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  status = pam_set_item(handle, PAM_TTY, "/dev/pts/9000");
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  status = pam_putenv(handle, "PATH=/path");
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  status = EscalateModuleMain(ESCALATE_MESSAGE_ACTION_AUTHENTICATE, handle, 0,
                              0, NULL);
  g_assert_cmpint(PAM_SYSTEM_ERR, ==, status);

  MockConversationAssertFinished(&conversation);
  pam_end(handle, PAM_SYSTEM_ERR);
}


void TestAddEnv() {
  struct pam_conv conversation = {
    MockConversationCallback, MockConversationNew(mock_auth_null_messages) };
  pam_handle_t *handle = NULL;
  int status = PAM_SYSTEM_ERR;
  const gchar *module_argv [] = { "add_env=FOO,BAR,NOPE" };
  GError *error = NULL;
  EscalateModule *module = NULL;

  EscalateTestSetMockHelperMessages(mock_auth_null_expect,
                                    mock_auth_null_respond);

  status = pam_start("mockservice", "janedoe", &conversation, &handle);
  g_assert_cmpint(PAM_SUCCESS, ==, status);

  // Process environment should never overwrite PAM environment.
  status = pam_putenv(handle, "FOO=good-value-for-foo");
  g_assert_cmpint(PAM_SUCCESS, ==, status);
  g_assert_cmpint(setenv("FOO", "bad-value-for-foo", 1), ==, 0);

  // Process environment can be used if it's not already set.
  g_assert_cmpint(setenv("BAR", "good-value-for-bar", 1), ==, 0);

  // Only process environment variables that are listed can be included.
  g_assert_cmpint(setenv("BAZ", "good-value-for-baz", 1), ==, 0);

  module = EscalateModuleNew(handle, 0, G_N_ELEMENTS(module_argv), module_argv,
                             NULL, &error);
  g_assert_no_error(error);
  g_assert(module);

  g_assert_cmpstr("good-value-for-foo", ==, pam_getenv(handle, "FOO"));
  g_assert_cmpstr("good-value-for-bar", ==, pam_getenv(handle, "BAR"));
  g_assert_cmpstr(NULL, ==, pam_getenv(handle, "BAZ"));
  g_assert_cmpstr(NULL, ==, pam_getenv(handle, "NOPE"));

  EscalateModuleFree(module);
  pam_end(handle, PAM_SUCCESS);
  MockConversationAssertFinished(&conversation);

  g_assert_cmpint(unsetenv("FOO"), ==, 0);
  g_assert_cmpint(unsetenv("BAR"), ==, 0);
  g_assert_cmpint(unsetenv("BAZ"), ==, 0);
}


int main(int argc, char **argv) {
  CacheTestInit();
  CacheTestInitUsersAndGroups();
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/escalate_module_test/TestAuthSuccess", TestAuthSuccess);
  g_test_add_func("/escalate_module_test/TestAuthErr", TestAuthErr);
  g_test_add_func("/escalate_module_test/TestAuthSystemErr", TestAuthSystemErr);
  g_test_add_func("/escalate_module_test/TestAddEnv", TestAddEnv);
  return g_test_run();
}
