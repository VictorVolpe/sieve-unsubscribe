# sieve-unsubscribe
Sieve Unsubscribe Helper

`/usr/local/etc/dovecot/dovecot.conf`:
```ini
protocols = sieve ...

protocol lmtp {
  ...
  mail_plugins = $mail_plugins sieve ...
}

protocol imap {
  ...
  mail_plugins = $mail_plugins imap_sieve ...
}

plugin {
  ...
  sieve = file:~/sieve;active=~/.dovecot.sieve
  sieve_plugins = sieve_extprograms
  sieve_extensions = +vnd.dovecot.execute
  sieve_execute_bin_dir = /mailhome/sieve/execute
}
```

# Install:
```sh
mv unsubscribe.sieve /mailhome/domains/mydomain.com/unsubscribe/sieve
chown vmail:vmail /mailhome/domains/mydomain.com/unsubscribe/sieve/unsubscribe.sieve
chmod 600 /mailhome/domains/mydomain.com/unsubscribe/sieve/unsubscribe.sieve
ln -sf /mailhome/domains/mydomain.com/unsubscribe/sieve/unsubscribe.sieve /mailhome/domains/mydomain.com/unsubscribe/.dovecot.sieve
chown vmail:vmail /mailhome/domains/mydomain.com/unsubscribe/.dovecot.sieve
chmod 600 /mailhome/domains/mydomain.com/unsubscribe/.dovecot.sieve
clang -o unsubscribe unsubscribe.c `mysql_config --cflags --libs` `pkg-config --cflags --libs libcurl`
mv unsubscribe /mailhome/sieve/execute
chown vmail:vmail /mailhome/sieve/execute/unsubscribe
touch /var/log/sieve-unsubscribe.log
chown vmail:vmail /var/log/sieve-unsubscribe.log
chmod 600 /var/log/sieve-unsubscribe.log
service dovecot restart
```
