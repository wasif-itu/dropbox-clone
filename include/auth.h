#ifndef AUTH_H
#define AUTH_H

int auth_init(void);
void auth_shutdown(void);

/* returns 0 on success, -1 if user exists / bad creds */
int auth_signup(const char *username, const char *password);
int auth_login(const char *username, const char *password);

#endif /* AUTH_H */
