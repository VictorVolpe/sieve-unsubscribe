/*
 * Sieve List-Unsubscribe Helper
 * Copyright (C) Victor Volpe
 *
 * clang -o unsubscribe unsubscribe.c `mysql_config --cflags --libs` `pkg-config --cflags --libs libcurl`
 */

#define DB_HOST "localhost"
#define DB_USER "postfix"
#define DB_PASS ""
#define DB_NAME "postfix"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <curl/curl.h>

MYSQL *mysql = NULL;
int exit_status = EXIT_FAILURE;

void output(char *format, ...)
{
	FILE *fp = fopen("/var/log/sieve-unsubscribe.log", "a");
	time_t current_time = time(NULL);
	struct tm *local_time = localtime(&current_time);
	va_list args;
	
	fprintf(fp, "%d-%02d-%02d %02d:%02d:%02d ", local_time->tm_year + 1900, local_time->tm_mon + 1, local_time->tm_mday, local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
	
	va_start(args, format);
	vfprintf(fp, format, args);
	va_end(args);
	
	fprintf(fp, "\n");
	fclose(fp);
}

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    return size * nmemb;
}

bool api_call(char *address, char *domain, char *list)
{
	CURL *curl;
	CURLcode res;
	char errbuf[CURL_ERROR_SIZE];
	char urlbuf[128];
	char postbuf[256];
	
	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	curl = curl_easy_init();
	
	if (!curl)
	{
		output("curl_easy_init() failed");
		
		curl_global_cleanup();
		
		return false;
	}
	
	snprintf(urlbuf, sizeof(urlbuf), "https://www.%s/unsubscribe.php", domain);
	snprintf(postbuf, sizeof(postbuf), "address=%s&list=%s", address, list);
	
	curl_easy_setopt(curl, CURLOPT_URL, urlbuf);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postbuf);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Sieve List-Unsubscribe Helper/1.0");
	curl_easy_setopt(curl, CURLOPT_USERPWD, "username:password");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	
	errbuf[0] = 0;
	
	res = curl_easy_perform(curl);
	
	if (res != CURLE_OK)
	{
		output("API call failed: %s", (strlen(errbuf)) ? errbuf : curl_easy_strerror(res));
		
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		
		return false;
	}
	
	output("API call '%s-%s-%s'", address, domain, list);
	
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	
	return true;
}

bool db_init()
{
	if (mysql_library_init(0, NULL, NULL))
	{
		output("mysql_library_init() failed");
		
		return false;
	}
	
	mysql = mysql_init(NULL);
	
	if (!mysql)
	{
		output("mysql_init() failed");
		mysql_library_end();
		
		return false;
	}
	
	if (!mysql_real_connect(mysql, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0))
	{
		output("mysql_real_connect() failed: %s", mysql_error(mysql));
		mysql_close(mysql);
		mysql_library_end();
		
		return false;
	}
	
	return true;
}

void db_end()
{
	mysql_close(mysql);
	mysql_library_end();
}

void db_store(char *address, char *domain, char *list)
{
	MYSQL_STMT *stmt;
	MYSQL_BIND bind[3];
	char query[] = "INSERT INTO sieve_unsubscribe (address, domain, list) VALUES (?, ?, ?)";
	unsigned long str_length[3];
	
	if (!db_init())
	{
		return;
	}
	
	stmt = mysql_stmt_init(mysql);
	
	if (!stmt)
	{
		output("mysql_stmt_init() failed");
		db_end();
		
		return;
	}
	
	str_length[0] = strlen(address);
	str_length[1] = strlen(domain);
	str_length[2] = strlen(list);
	
	memset(bind, 0, sizeof(bind));
	
	bind[0].buffer_type = MYSQL_TYPE_STRING;
	bind[0].buffer = address;
	bind[0].buffer_length = str_length[0] + 1;
	bind[0].is_null = 0;
	bind[0].length = &str_length[0];
	
	bind[1].buffer_type = MYSQL_TYPE_STRING;
	bind[1].buffer = domain;
	bind[1].buffer_length = str_length[1] + 1;
	bind[1].is_null = 0;
	bind[1].length = &str_length[1];
	
	bind[2].buffer_type = MYSQL_TYPE_STRING;
	bind[2].buffer = list;
	bind[2].buffer_length = str_length[2] + 1;
	bind[2].is_null = 0;
	bind[2].length = &str_length[2];
	
	if (mysql_stmt_prepare(stmt, query, strlen(query)))
	{
		output("mysql_stmt_prepare() failed: %s", mysql_stmt_error(stmt));
		db_end();
		
		return;
	}
	
	if (mysql_stmt_bind_param(stmt, bind))
	{
		output("mysql_stmt_bind_param() failed: %s", mysql_stmt_error(stmt));
		db_end();
		
		return;
	}
	
	if (mysql_stmt_execute(stmt))
	{
		if (mysql_stmt_errno(stmt) == ER_DUP_ENTRY)
		{
			output("Duplicate entry '%s-%s-%s'", address, domain, list);
			
			exit_status = EXIT_SUCCESS;
		}
		else
		{
			output("mysql_stmt_execute() failed: %s", mysql_stmt_error(stmt));
		}
		
		db_end();
		
		return;
	}
	
	output("New entry '%s-%s-%s'", address, domain, list);
	
	if (mysql_stmt_close(stmt))
	{
		output("mysql_stmt_close() failed: %s", mysql_error(mysql));
		db_end();
		
		return;
	}
	
	db_end();
	
	exit_status = EXIT_SUCCESS;
}

void db_retrieve()
{
	MYSQL_STMT *stmt;
	MYSQL_BIND bind[3];
	char query[] = "SELECT address, domain, list FROM sieve_unsubscribe";
	char query2[1024];
	char str_data[3][256];
	unsigned long str_length[3];
	bool str_isnull[3];
	bool str_error[3];
	
	if (!db_init())
	{
		return;
	}
	
	stmt = mysql_stmt_init(mysql);
	
	if (!stmt)
	{
		output("mysql_stmt_init() failed");
		db_end();
		
		return;
	}
	
	if (mysql_stmt_prepare(stmt, query, strlen(query)))
	{
		output("mysql_stmt_prepare() failed: %s", mysql_stmt_error(stmt));
		db_end();
		
		return;
	}
	
	if (mysql_stmt_execute(stmt))
	{
		output("mysql_stmt_execute() failed: %s", mysql_stmt_error(stmt));
		db_end();
		
		return;
	}
	
	memset(bind, 0, sizeof(bind));
	
	bind[0].buffer_type = MYSQL_TYPE_STRING;
	bind[0].buffer = (char *)str_data[0];
	bind[0].buffer_length = 256;
	bind[0].is_null = &str_isnull[0];
	bind[0].length = &str_length[0];
	bind[0].error = &str_error[0];
	
	bind[1].buffer_type = MYSQL_TYPE_STRING;
	bind[1].buffer = (char *)str_data[1];
	bind[1].buffer_length = 256;
	bind[1].is_null = &str_isnull[1];
	bind[1].length = &str_length[1];
	bind[1].error = &str_error[1];
	
	bind[2].buffer_type = MYSQL_TYPE_STRING;
	bind[2].buffer = (char *)str_data[2];
	bind[2].buffer_length = 256;
	bind[2].is_null = &str_isnull[2];
	bind[2].length = &str_length[2];
	bind[2].error = &str_error[2];
	
	if (mysql_stmt_bind_result(stmt, bind))
	{
		output("mysql_stmt_bind_result() failed: %s", mysql_stmt_error(stmt));
		db_end();
		
		return;
	}
	
	if (mysql_stmt_store_result(stmt))
	{
		output("mysql_stmt_store_result() failed: %s", mysql_stmt_error(stmt));
		db_end();
		
		return;
	}
	
	while (!mysql_stmt_fetch(stmt))
	{
		if (api_call(str_data[0], str_data[1], str_data[2]))
		{
			snprintf(query2, sizeof(query2), "DELETE FROM sieve_unsubscribe WHERE address='%s' AND domain='%s' AND list='%s'", str_data[0], str_data[1], str_data[2]);
			
			if (mysql_query(mysql, query2))
			{
				output("mysql_query() failed: %s", mysql_error(mysql));
				
				exit_status = 2;
				
				break;
			}
			
			output("Remove entry '%s-%s-%s'", str_data[0], str_data[1], str_data[2]);
		}
		else
		{
			break;
		}
	}
	
	if (mysql_stmt_close(stmt))
	{
		output("mysql_stmt_close() failed: %s", mysql_error(mysql));
		db_end();
		
		return;
	}
	
	db_end();
	
	if (exit_status == 2)
	{
		exit_status = EXIT_FAILURE;
	}
	else
	{
		exit_status = EXIT_SUCCESS;
	}
}

int main(int argc, char *argv[])
{
	char *address;
	char *domain;
	char *list;
	
	if (argc == 1)
	{
		db_retrieve();
		
		return exit_status;
	}
	else if (argc != 4 || daemon(0, 0) == -1)
	{
		return EXIT_FAILURE;
	}
	
	address = malloc(strlen(argv[1]) + 1);
	domain = malloc(strlen(argv[2]) + 1);
	list = malloc(strlen(argv[3]) + 1);
	
	strcpy(address, argv[1]);
	strcpy(domain, argv[2]);
	strcpy(list, argv[3]);
	
	if (!api_call(address, domain, list))
	{
		db_store(address, domain, list);
	}
	
	free(address);
	free(domain);
	free(list);
	
	return exit_status;
}