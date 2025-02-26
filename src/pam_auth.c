#include "pam_auth.h"
#include <security/pam_appl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int pam_conv_func(int num_msg, const struct pam_message **msg,
                         struct pam_response **resp, void *appdata_ptr) {
    if (num_msg <= 0)
        return PAM_CONV_ERR;
    *resp = (struct pam_response *)calloc(num_msg, sizeof(struct pam_response));
    if (*resp == NULL)
        return PAM_BUF_ERR;
    const char *password = (const char *)appdata_ptr;
    for (int i = 0; i < num_msg; i++) {
        switch(msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON:
                (*resp)[i].resp = strdup(password);
                (*resp)[i].resp_retcode = 0;
                break;
            case PAM_TEXT_INFO:
            case PAM_ERROR_MSG:
                (*resp)[i].resp = NULL;
                break;
            default:
                free(*resp);
                return PAM_CONV_ERR;
        }
    }
    return PAM_SUCCESS;
}

int authenticate_user(const char *username, const char *password) {
    pam_handle_t *pamh = NULL;
    int retval;
    struct pam_conv conv = {
        .conv = pam_conv_func,
        .appdata_ptr = (void *)password
    };

    /* Use the "fblogin" PAM service (ensure /etc/pam.d/fblogin exists) */
    retval = pam_start("fblogin", username, &conv, &pamh);
    if(retval != PAM_SUCCESS) {
        fprintf(stderr, "pam_start failed: %s\n", pam_strerror(pamh, retval));
        return -1;
    }
    
    /* Tell PAM which TTY we're using */
    pam_set_item(pamh, PAM_TTY, "/dev/tty1");
    
    retval = pam_authenticate(pamh, 0);
    if(retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        return -1;
    }
    
    retval = pam_acct_mgmt(pamh, 0);
    if(retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        return -1;
    }
    
    pam_end(pamh, PAM_SUCCESS);
    return 0;
}

