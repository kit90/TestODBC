#include <Windows.h>
#include <sql.h>
#include <sqlext.h>
#include <locale.h>
#include <wchar.h>
#include <stdlib.h>

#define DISPLAY_FORMAT L"%*.*ls|"
#define DISPLAY_MAX 20          // Arbitrary limit on column width to display.
#define MAX_WORD_LEN 63
#define MAX_SENTENCE_LEN 255

/* Helper function to display diagnostics. */
void displayErrors(SQLHANDLE handle, SQLSMALLINT type, SQLRETURN returnCode) 
{
    fwprintf(stderr, L"Error!\n");

    if (returnCode == SQL_INVALID_HANDLE) 
    {
        fwprintf(stderr, L"Invalid handle!\n");
        return;
    }

    SQLSMALLINT	 i = 0;
    SQLINTEGER	 native;
    SQLWCHAR	 state[SQL_SQLSTATE_SIZE + 1];
    SQLWCHAR	 text[SQL_MAX_MESSAGE_LENGTH];
    SQLSMALLINT	 len;
    SQLRETURN	 ret;

    while (SQL_SUCCEEDED(ret = SQLGetDiagRecW(type, handle, ++i, state, &native, 
        text, sizeof text / sizeof(SQLWCHAR), &len))) 
    {
        fwprintf(stderr, 			
            L"Status record: %hd, SQLSTATE: %ls, Native error code: %ld, "
            L"Diagnostic message: %ls.\n", 
            i, state, native, text);
    }

    fwprintf(stderr, L"\n");
}

/* Function to display the results of a query. */
void displayResults(SQLHSTMT stmt) 
{
    SQLRETURN ret;

    wprintf(L"\n");

    /* Check if this is a row-returning query. */
    SQLSMALLINT columns; // number of columns in result-set 
    ret = SQLNumResultCols(stmt, &columns);

    /* If this is not a row-returning query, display
       the number of rows affected by the statement. */	
    if (columns <= 0) 
    {
        SQLLEN rowsAffected;
        ret = SQLRowCount(stmt, &rowsAffected);

        if (rowsAffected >= 0) 
        {
            wprintf(L"%Id row(s) affected.\n\n", rowsAffected);
        }

        return;
    }
    
    SQLSMALLINT i;

    /* If this is a row-returning query, display results. */
    for (i = 0; i < columns; i++)
    {
        SQLWCHAR		columnName[MAX_WORD_LEN + 1];
        SQLSMALLINT     columnNameLength;

        /* Fetch the column name. */
        ret = SQLColAttributeW(stmt, i + 1, SQL_DESC_NAME,
            columnName, sizeof columnName, // Note count of bytes!
            &columnNameLength, NULL);

        /* Display the column name. */
        wprintf(DISPLAY_FORMAT, DISPLAY_MAX, DISPLAY_MAX, columnName);
    }
    wprintf(L"\n");

    /* Print a separator bar for the column names. */	
    for (i = 0; i < columns; i++)
    {
        for (int j = 0; j < DISPLAY_MAX; j++) 
        {
            wprintf(L"-");
        }
        wprintf(L"|");
    }
    wprintf(L"\n");

    /* Loop through the columns in the result-set binding to local variables. */
    SQLWCHAR    (*buf)[MAX_WORD_LEN + 1] = (SQLWCHAR (*)[MAX_WORD_LEN + 1])
        malloc(columns * (MAX_WORD_LEN + 1) * sizeof(SQLWCHAR));
    SQLLEN		*indicator = (SQLLEN *) malloc(columns * sizeof(SQLLEN));
    for (i = 0; i < columns; i++) 
    {
        /* Map this buffer to the driver's buffer.   At Fetch time,
           the driver will fill in this data.  Note that the size is 
           count of bytes (for Unicode).  All ODBC functions that take
           SQLPOINTER use count of bytes; all functions that take only
           strings use count of characters. */
        ret = SQLBindCol(stmt, i + 1, SQL_C_WCHAR,
            buf[i], sizeof buf[i], &indicator[i]);
    }

    /* Fetch the data. */
    SQLLEN rowsReturned = 0;
    while (SQL_SUCCEEDED(ret = SQLFetch(stmt))) 
    {
        /* Display the results that will now be in the bound areas. */
        for (i = 0; i < columns; i++) 
        {
            if (indicator[i] == SQL_NULL_DATA) 
            {
                wprintf(DISPLAY_FORMAT, DISPLAY_MAX, DISPLAY_MAX, L"<NULL>");
            }
            else 
            {
                wprintf(DISPLAY_FORMAT, DISPLAY_MAX, DISPLAY_MAX, buf[i]);
            }
        }

        wprintf(L"\n");
        rowsReturned++;
    }

    free(indicator);
    free(buf);

    wprintf(L"\n%Id row(s) returned.\n\n", rowsReturned);
}

/* Function to process queries in a loop. */
void processStatements(SQLHSTMT stmt)
{
    SQLRETURN ret;

    wprintf(L"Enter SQL commands.\n"
        L"Type 'tables' to list the tables.\n"
        L"Type 'columns <table>' to list the columns of <table>.\n"
        L"Type 'quit' to quit.\n\n");

    /* Loop to get input and execute queries. */
    wprintf(L"SQL> ");
    SQLWCHAR statementString[MAX_SENTENCE_LEN + 1];
    while (fgetws(statementString, sizeof statementString / sizeof(SQLWCHAR), stdin) != NULL)
    {
        if (wcsstr(statementString, L"tables") == statementString)
        {
            /* Retrieve a list of tables. */
            ret = SQLTablesW(stmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0);
        }
        else if (wcsstr(statementString, L"columns") == statementString)
        {
            const wchar_t *delim = L" \n";
            wchar_t *next_token;

            wchar_t *token = wcstok_s(statementString, delim, &next_token);
            int i = 1;
            while (token != NULL && i < 2) {
                token = wcstok_s(NULL, delim, &next_token);
                i++;
            }

            /* Retrieve a list of columns. */
            ret = SQLColumnsW(stmt, NULL, 0, NULL, 0, token, SQL_NTS, NULL, 0);
        }
        else if (wcsstr(statementString, L"quit") == statementString)
        {
            break;
        }
        else
        {
            /* Execute the SQL statement statementString. */
            ret = SQLExecDirectW(stmt, statementString, SQL_NTS);
        }

        if (ret != SQL_SUCCESS)
        {
            displayErrors(stmt, SQL_HANDLE_STMT, ret);
        }
        if (ret != SQL_ERROR)
        {
            displayResults(stmt);

            ret = SQLFreeStmt(stmt, SQL_CLOSE);
        }

        wprintf(L"SQL> ");
    }
}

int wmain(int argc, wchar_t **argv) 
{
    _wsetlocale(LC_ALL, L"");

    if (argc != 2) 
    {
        fwprintf(stderr, L"Usage: %ls <connection string>\n", argv[0]);		
        return EXIT_FAILURE;
    }

    SQLRETURN	ret; // ODBC API return status. 
    SQLHENV		env = NULL;
    SQLHDBC		dbc = NULL;
    SQLHSTMT	stmt = NULL;
    int			exitStatus = EXIT_SUCCESS;

    /* Allocate an environment handle. */	
    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);

    /* We want ODBC 3 support. */
    ret = SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);

    /* Allocate a connection handle. */
    ret = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);

    /* Connect to the DSN in connectionString. */
    SQLWCHAR *connectionString = argv[1];
    ret = SQLDriverConnectW(dbc, NULL, connectionString, SQL_NTS,
                     NULL, 0, NULL, SQL_DRIVER_COMPLETE);
    if (ret != SQL_SUCCESS)
    {
        displayErrors(dbc, SQL_HANDLE_DBC, ret);
    }
    if (ret == SQL_ERROR)
    {
        exitStatus = EXIT_FAILURE;
        goto Exit;
    }

    /* Allocate a statement handle. */
    ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    processStatements(stmt);

Exit:
    /* Free ODBC handles and exit. */
    if (stmt) 
    {
        ret = SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    }

    if (dbc) 
    {
        ret = SQLDisconnect(dbc);
        ret = SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    }

    if (env) 
    {
        ret = SQLFreeHandle(SQL_HANDLE_ENV, env);
    }

    return exitStatus;
}
