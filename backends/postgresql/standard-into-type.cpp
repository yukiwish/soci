//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_POSTGRESQL_SOURCE
#include "soci-postgresql.h"
#include "common.h"
#include "rowid.h"
#include "blob.h"
#include <libpq/libpq-fs.h> // libpq
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef SOCI_PGSQL_NOPARAMS
#define SOCI_PGSQL_NOBINDBYNAME
#endif // SOCI_PGSQL_NOPARAMS

#ifdef _MSC_VER
#pragma warning(disable:4355 4996)
#define strtoll(s, p, b) static_cast<long long>(_strtoi64(s, p, b))
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::postgresql;


void postgresql_standard_into_type_backend::define_by_pos(
    int &position, void *data, exchange_type type)
{
    data_ = data;
    type_ = type;
    position_ = position++;
}

void postgresql_standard_into_type_backend::pre_fetch()
{
    // nothing to do here
}

void postgresql_standard_into_type_backend::post_fetch(
    bool gotData, bool calledFromFetch, indicator *ind)
{
    if (calledFromFetch == true && gotData == false)
    {
        // this is a normal end-of-rowset condition,
        // no need to do anything (fetch() will return false)
        return;
    }

    if (gotData)
    {
        // postgresql_ positions start at 0
        int pos = position_ - 1;

        // first, deal with indicators
        if (PQgetisnull(statement_.result_, statement_.currentRow_, pos) != 0)
        {
            if (ind == NULL)
            {
                throw soci_error(
                    "Null value fetched and no indicator defined.");
            }

            *ind = i_null;

            // no need to convert data if it is null
            return;
        }
        else
        {
            if (ind != NULL)
            {
                *ind = i_ok;
            }
        }

        // raw data, in text format
        char *buf = PQgetvalue(statement_.result_,
            statement_.currentRow_, pos);

        switch (type_)
        {
        case x_char:
            {
                char *dest = static_cast<char*>(data_);
                *dest = *buf;
            }
            break;
        case x_cstring:
            {
                cstring_descriptor *strDescr
                    = static_cast<cstring_descriptor *>(data_);

                std::strncpy(strDescr->str_, buf, strDescr->bufSize_ - 1);
                strDescr->str_[strDescr->bufSize_ - 1] = '\0';

                if (std::strlen(buf) >= strDescr->bufSize_ && ind != NULL)
                {
                    *ind = i_truncated;
                }
            }
            break;
        case x_stdstring:
            {
                std::string *dest = static_cast<std::string *>(data_);
                dest->assign(buf);
            }
            break;
        case x_short:
            {
                short *dest = static_cast<short*>(data_);
                char * end;
                long val = strtol(buf, &end, 10);
                check_integer_conversion(buf, end, val);
                *dest = static_cast<short>(val);
            }
            break;
        case x_integer:
            {
                int *dest = static_cast<int*>(data_);
                char * end;
                long val = strtol(buf, &end, 10);
                check_integer_conversion(buf, end, val);
                *dest = static_cast<int>(val);
            }
            break;
        case x_unsigned_long:
            {
                unsigned long *dest = static_cast<unsigned long *>(data_);
                char * end;
                long long val = strtoll(buf, &end, 10);
                check_integer_conversion(buf, end, val);
                *dest = static_cast<unsigned long>(val);
            }
            break;
        case x_long_long:
            {
                long long *dest = static_cast<long long *>(data_);
                char * end;
                long long val = strtoll(buf, &end, 10);
                check_integer_conversion(buf, end, val);
                *dest = val;
            }
            break;
        case x_double:
            {
                double *dest = static_cast<double*>(data_);
                char * end;
                double val = strtod(buf, &end);
                if (end == buf)
                {
                    throw soci_error("Cannot convert data.");
                }
                *dest = val;
            }
            break;
        case x_stdtm:
            {
                // attempt to parse the string and convert to std::tm
                std::tm *dest = static_cast<std::tm *>(data_);
                parse_std_tm(buf, *dest);
            }
            break;
        case x_rowid:
            {
                // RowID is internally identical to unsigned long

                rowid *rid = static_cast<rowid *>(data_);
                postgresql_rowid_backend *rbe
                    = static_cast<postgresql_rowid_backend *>(
                        rid->get_backend());

                char * end;
                long long val = strtoll(buf, &end, 10);
                if (end == buf)
                {
                    throw soci_error("Cannot convert data.");
                }
                rbe->value_ = static_cast<unsigned long>(val);
            }
            break;
        case x_blob:
            {
                char * end;
                long long llval = strtoll(buf, &end, 10);
                if (end == buf)
                {
                    throw soci_error("Cannot convert data.");
                }
                unsigned long oid = static_cast<unsigned long>(llval);

                int fd = lo_open(statement_.session_.conn_, oid,
                    INV_READ | INV_WRITE);
                if (fd == -1)
                {
                    throw soci_error("Cannot open the blob object.");
                }

                blob *b = static_cast<blob *>(data_);
                postgresql_blob_backend *bbe
                     = static_cast<postgresql_blob_backend *>(b->get_backend());

                if (bbe->fd_ != -1)
                {
                    lo_close(statement_.session_.conn_, bbe->fd_);
                }

                bbe->fd_ = fd;
            }
            break;

        default:
            throw soci_error("Into element used with non-supported type.");
        }
    }
}

void postgresql_standard_into_type_backend::clean_up()
{
    // nothing to do here
}
