#include <Windows.h>
#include <sql.h>
#include <sqlext.h>
#include <locale.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdbool.h>

#define DISPLAY_MAX          50            // Arbitrary limit on column width to display.
#define DISPLAY_FORMAT_EXTRA 3             // Per column extra display bytes ( <data> |).
#define DISPLAY_FORMAT       L" %*.*ls |"
#define DISPLAY_FORMAT_C     L" %-*.*ls |"
#define NULL_SIZE            6             // <NULL>.
#define QUERY_MAX            1000          // Maximum number of characters for SQL query passed in.

/* Structure to store information about a column. */
struct column_t
{
    SQLWCHAR *columnName;               // column name.
    SQLWCHAR *buf;                      // display buffer.
    SQLLEN indicator;                   // size or null.
    SQLSMALLINT cDisplaySize;           // size to display.
    bool fChar;                         // character column?
};

void displayErrors(SQLHANDLE handle, SQLSMALLINT type, SQLRETURN returnCode);
void displayResults(SQLHSTMT stmt);
void processStatements(SQLHSTMT stmt);

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

    /* Allocate an environment handle. */	
    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);

    /* We want ODBC 3 support. */
    ret = SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    /* Allocate a connection handle. */
    ret = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);

    /* Connect to the DSN in connectionString. */
    SQLWCHAR *connectionString = argv[1];
    ret = SQLDriverConnectW(dbc, NULL, 
        connectionString, SQL_NTS,                     
        NULL, 0, NULL, 
        SQL_DRIVER_COMPLETE);
    if (ret != SQL_SUCCESS)
    {
        displayErrors(dbc, SQL_HANDLE_DBC, ret);
    }
    if (ret == SQL_ERROR)
    {
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

    return EXIT_SUCCESS;
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
    SQLWCHAR statementString[QUERY_MAX + 2]; // Reserve space for '\n' and '\0'.
    while (fgetws(statementString, sizeof statementString / sizeof(SQLWCHAR), stdin) != NULL)
    {
        if (wcsstr(statementString, L"tables") == statementString)
        {
            /* Retrieve a list of tables. */
            ret = SQLTablesW(stmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0);
        }
        else if (wcsstr(statementString, L"columns") == statementString)
        {
            const wchar_t *delim = L" \t\n\v\f\r"; // All whitespace characters.
            wchar_t *next_token;

            wchar_t *token = wcstok_s(statementString, delim, &next_token);
            int i = 1;
            while (token != NULL && i < 2)
            {
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

/* Function to display the results of a query. */
void displayResults(SQLHSTMT stmt)
{
    SQLRETURN ret;

    wprintf(L"\n");

    /* Check if this is a row-returning query. */
    SQLSMALLINT columnCount; // number of columns in result-set 
    ret = SQLNumResultCols(stmt, &columnCount);

    /* If this is not a row-returning query, display
    the number of rows affected by the statement. */
    if (columnCount <= 0)
    {
        SQLLEN rowsAffected;
        ret = SQLRowCount(stmt, &rowsAffected);

        if (rowsAffected >= 0)
        {
            wprintf(L"%Id row(s) affected.\n\n", rowsAffected);
        }

        return;
    }

    /* If this is a row-returning query, display results. */
    struct column_t *columns = (struct column_t *)malloc(columnCount * sizeof(struct column_t));
    SQLSMALLINT i;

    for (i = 0; i < columnCount; i++)
    {
        /* Figure out the length of the column name. */
        SQLSMALLINT columnNameLength;
        ret = SQLColAttributeW(stmt, i + 1, SQL_DESC_NAME,
            NULL, 0, &columnNameLength, 
            NULL);

        columns[i].columnName = (SQLWCHAR *)malloc(columnNameLength + sizeof(SQLWCHAR));

        /* Fetch the column name. */
        ret = SQLColAttributeW(stmt, i + 1, SQL_DESC_NAME,
            columns[i].columnName, columnNameLength + sizeof(SQLWCHAR), NULL,
            NULL);

        /* Figure out the display length of the column. */
        SQLLEN cchDisplay;
        ret = SQLColAttributeW(stmt, i + 1, SQL_DESC_DISPLAY_SIZE,
            NULL, 0, NULL, 
            &cchDisplay);

        /* Allocate a buffer big enough to hold the text representation
           of the data. Add one character for the null terminator. */
        columns[i].buf = (SQLWCHAR *)malloc((cchDisplay + 1) * sizeof(SQLWCHAR));

        /* Map this buffer to the driver's buffer. At Fetch time,
           the driver will fill in this data. Note that the size is
           count of bytes (for Unicode). All ODBC functions that take
           SQLPOINTER use count of bytes; all functions that take only
           strings use count of characters. */
        ret = SQLBindCol(stmt, i + 1, SQL_C_WCHAR,
            columns[i].buf, (cchDisplay + 1) * sizeof(SQLWCHAR), &columns[i].indicator);

        columns[i].cDisplaySize = max((SQLSMALLINT)cchDisplay, NULL_SIZE);
        columns[i].cDisplaySize = max(columns[i].cDisplaySize, columnNameLength / sizeof(SQLWCHAR));
        columns[i].cDisplaySize = min(columns[i].cDisplaySize, DISPLAY_MAX);

        /* Figure out if this is a character or numeric column; this is
           used to determine if we want to display the data left- or right-
           aligned.

           SQL_DESC_CONCISE_TYPE maps to the 1.x SQL_COLUMN_TYPE.
           This is what you must use if you want to work
           against a 2.x driver. */
        SQLLEN ssType;
        ret = SQLColAttributeW(stmt, i + 1, SQL_DESC_CONCISE_TYPE,
            NULL, 0, NULL, 
            &ssType);
        
        columns[i].fChar = ssType == SQL_CHAR
            || ssType == SQL_VARCHAR
            || ssType == SQL_LONGVARCHAR;

        /* Display the column name. */
        wprintf(DISPLAY_FORMAT_C, columns[i].cDisplaySize, columns[i].cDisplaySize, 
            columns[i].columnName);
    }
    wprintf(L"\n");

    /* Print a separator bar for the column names. */
    for (i = 0; i < columnCount; i++)
    {
        for (int j = 0; j < columns[i].cDisplaySize + DISPLAY_FORMAT_EXTRA - 1; j++)
        {
            wprintf(L"-");
        }
        wprintf(L"|");
    }
    wprintf(L"\n");

    /* Fetch the data. */
    SQLLEN rowsReturned = 0;
    while (SQL_SUCCEEDED(ret = SQLFetch(stmt)))
    {
        /* Display the results that will now be in the bound areas. */
        for (i = 0; i < columnCount; i++)
        {
            if (columns[i].indicator == SQL_NULL_DATA)
            {
                wprintf(columns[i].fChar ? DISPLAY_FORMAT_C : DISPLAY_FORMAT,
                    columns[i].cDisplaySize, columns[i].cDisplaySize, L"<NULL>");
            }
            else
            {
                wprintf(columns[i].fChar ? DISPLAY_FORMAT_C : DISPLAY_FORMAT, 
                    columns[i].cDisplaySize, columns[i].cDisplaySize, columns[i].buf);
            }
        }

        wprintf(L"\n");
        rowsReturned++;
    }
    wprintf(L"\n%Id row(s) returned.\n\n", rowsReturned);

    for (i = 0; i < columnCount; i++)
    {
        free(columns[i].buf);
        free(columns[i].columnName);
    }

    free(columns);
}

/* Helper function to display diagnostics. */
void displayErrors(SQLHANDLE handle, SQLSMALLINT type, SQLRETURN returnCode)
{
    if (returnCode == SQL_INVALID_HANDLE)
    {
        fwprintf(stderr, L"Error: Invalid handle.\n\n");
        return;
    }

    SQLSMALLINT	 i = 0;
    SQLINTEGER	 native;
    SQLWCHAR	 state[SQL_SQLSTATE_SIZE + 1];
    SQLWCHAR	 text[SQL_MAX_MESSAGE_LENGTH + 1];
    SQLSMALLINT	 len;
    SQLRETURN	 ret;

    while (SQL_SUCCEEDED(ret = SQLGetDiagRecW(type, handle, i + 1, state, &native,
        text, sizeof text / sizeof(SQLWCHAR), &len)))
    {
        fwprintf(stderr,
            L"[SQLSTATE: %ls][Native error code: %ld]\n%ls\n\n",
            state, native, text);

        i++;
    }
}
