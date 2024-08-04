/**
 * @file omni_int.c
 *
 */

// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on
#include <limits.h>

#include "omni_int.h"

/*
 * Copy of old pg_atoi() from PostgreSQL, cut down to support int8 only.
 */
static int8_t pg_atoi8(const char *s) {
  long result;
  char *badp;

  /*
   * Some versions of strtol treat the empty string as an error, but some
   * seem not to.  Make an explicit test to be sure we catch it.
   */
  if (s == NULL)
    elog(ERROR, "NULL pointer");
  if (*s == 0)
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid input syntax for type %s: \"%s\"", "integer", s)));

  errno = 0;
  result = strtol(s, &badp, 10);

  /* We made no progress parsing the string, so bail out */
  if (s == badp)
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid input syntax for type %s: \"%s\"", "integer", s)));

  if (errno == ERANGE || result < SCHAR_MIN || result > SCHAR_MAX)
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                    errmsg("value \"%s\" is out of range for 8-bit integer", s)));

  /*
   * Skip any trailing whitespace; if anything but whitespace remains before
   * the terminating character, bail out
   */
  while (*badp && isspace((unsigned char)*badp))
    badp++;

  if (*badp)
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid input syntax for type %s: \"%s\"", "integer", s)));

  return (int8)result;
}

static uint32 pg_atou(const char *s, int size) {
  unsigned long int result;
  bool out_of_range = false;
  char *badp;

  if (s == NULL)
    elog(ERROR, "NULL pointer");
  if (*s == 0)
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid input syntax for unsigned integer: \"%s\"", s)));

  if (strchr(s, '-'))
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid input syntax for unsigned integer: \"%s\"", s)));

  errno = 0;
  result = strtoul(s, &badp, 10);

  switch (size) {
  case sizeof(uint32):
    if (errno == ERANGE
#if defined(HAVE_LONG_INT_64)
        || result > UINT_MAX
#endif
    )
      out_of_range = true;
    break;
  case sizeof(uint16):
    if (errno == ERANGE || result > USHRT_MAX)
      out_of_range = true;
    break;
  case sizeof(uint8):
    if (errno == ERANGE || result > UCHAR_MAX)
      out_of_range = true;
    break;
  default:
    elog(ERROR, "unsupported result size: %d", size);
  }

  if (out_of_range)
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                    errmsg("value \"%s\" is out of range for type uint%d", s, size)));

  while (*badp && isspace((unsigned char)*badp))
    badp++;

  if (*badp)
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid input syntax for unsigned integer: \"%s\"", s)));

  return result;
}

static const char *uint8_t_strings[256] = {
    "0",   "1",   "2",   "3",   "4",   "5",   "6",   "7",   "8",   "9",   "10",  "11",  "12",
    "13",  "14",  "15",  "16",  "17",  "18",  "19",  "20",  "21",  "22",  "23",  "24",  "25",
    "26",  "27",  "28",  "29",  "30",  "31",  "32",  "33",  "34",  "35",  "36",  "37",  "38",
    "39",  "40",  "41",  "42",  "43",  "44",  "45",  "46",  "47",  "48",  "49",  "50",  "51",
    "52",  "53",  "54",  "55",  "56",  "57",  "58",  "59",  "60",  "61",  "62",  "63",  "64",
    "65",  "66",  "67",  "68",  "69",  "70",  "71",  "72",  "73",  "74",  "75",  "76",  "77",
    "78",  "79",  "80",  "81",  "82",  "83",  "84",  "85",  "86",  "87",  "88",  "89",  "90",
    "91",  "92",  "93",  "94",  "95",  "96",  "97",  "98",  "99",  "100", "101", "102", "103",
    "104", "105", "106", "107", "108", "109", "110", "111", "112", "113", "114", "115", "116",
    "117", "118", "119", "120", "121", "122", "123", "124", "125", "126", "127", "128", "129",
    "130", "131", "132", "133", "134", "135", "136", "137", "138", "139", "140", "141", "142",
    "143", "144", "145", "146", "147", "148", "149", "150", "151", "152", "153", "154", "155",
    "156", "157", "158", "159", "160", "161", "162", "163", "164", "165", "166", "167", "168",
    "169", "170", "171", "172", "173", "174", "175", "176", "177", "178", "179", "180", "181",
    "182", "183", "184", "185", "186", "187", "188", "189", "190", "191", "192", "193", "194",
    "195", "196", "197", "198", "199", "200", "201", "202", "203", "204", "205", "206", "207",
    "208", "209", "210", "211", "212", "213", "214", "215", "216", "217", "218", "219", "220",
    "221", "222", "223", "224", "225", "226", "227", "228", "229", "230", "231", "232", "233",
    "234", "235", "236", "237", "238", "239", "240", "241", "242", "243", "244", "245", "246",
    "247", "248", "249", "250", "251", "252", "253", "254", "255"};

static const char *int8_t_strings[256] = {
    "-128", "-127", "-126", "-125", "-124", "-123", "-122", "-121", "-120", "-119", "-118", "-117",
    "-116", "-115", "-114", "-113", "-112", "-111", "-110", "-109", "-108", "-107", "-106", "-105",
    "-104", "-103", "-102", "-101", "-100", "-99",  "-98",  "-97",  "-96",  "-95",  "-94",  "-93",
    "-92",  "-91",  "-90",  "-89",  "-88",  "-87",  "-86",  "-85",  "-84",  "-83",  "-82",  "-81",
    "-80",  "-79",  "-78",  "-77",  "-76",  "-75",  "-74",  "-73",  "-72",  "-71",  "-70",  "-69",
    "-68",  "-67",  "-66",  "-65",  "-64",  "-63",  "-62",  "-61",  "-60",  "-59",  "-58",  "-57",
    "-56",  "-55",  "-54",  "-53",  "-52",  "-51",  "-50",  "-49",  "-48",  "-47",  "-46",  "-45",
    "-44",  "-43",  "-42",  "-41",  "-40",  "-39",  "-38",  "-37",  "-36",  "-35",  "-34",  "-33",
    "-32",  "-31",  "-30",  "-29",  "-28",  "-27",  "-26",  "-25",  "-24",  "-23",  "-22",  "-21",
    "-20",  "-19",  "-18",  "-17",  "-16",  "-15",  "-14",  "-13",  "-12",  "-11",  "-10",  "-9",
    "-8",   "-7",   "-6",   "-5",   "-4",   "-3",   "-2",   "-1",   "0",    "1",    "2",    "3",
    "4",    "5",    "6",    "7",    "8",    "9",    "10",   "11",   "12",   "13",   "14",   "15",
    "16",   "17",   "18",   "19",   "20",   "21",   "22",   "23",   "24",   "25",   "26",   "27",
    "28",   "29",   "30",   "31",   "32",   "33",   "34",   "35",   "36",   "37",   "38",   "39",
    "40",   "41",   "42",   "43",   "44",   "45",   "46",   "47",   "48",   "49",   "50",   "51",
    "52",   "53",   "54",   "55",   "56",   "57",   "58",   "59",   "60",   "61",   "62",   "63",
    "64",   "65",   "66",   "67",   "68",   "69",   "70",   "71",   "72",   "73",   "74",   "75",
    "76",   "77",   "78",   "79",   "80",   "81",   "82",   "83",   "84",   "85",   "86",   "87",
    "88",   "89",   "90",   "91",   "92",   "93",   "94",   "95",   "96",   "97",   "98",   "99",
    "100",  "101",  "102",  "103",  "104",  "105",  "106",  "107",  "108",  "109",  "110",  "111",
    "112",  "113",  "114",  "115",  "116",  "117",  "118",  "119",  "120",  "121",  "122",  "123",
    "124",  "125",  "126",  "127"};

PG_FUNCTION_INFO_V1(int1_in);

Datum int1_in(PG_FUNCTION_ARGS) {
  char *s = PG_GETARG_CSTRING(0);

  PG_RETURN_INT8(pg_atoi8(s));
}

PG_FUNCTION_INFO_V1(int1_out);

Datum int1_out(PG_FUNCTION_ARGS) {
  int8_t arg1 = PG_GETARG_INT8(0);
  PG_RETURN_CSTRING(int8_t_strings[arg1 + 128]);
}

PG_FUNCTION_INFO_V1(uint1_in);

Datum uint1_in(PG_FUNCTION_ARGS) {
  char *s = PG_GETARG_CSTRING(0);
  PG_RETURN_UINT8(pg_atou(s, sizeof(uint8)));
}

PG_FUNCTION_INFO_V1(uint1_out);

Datum uint1_out(PG_FUNCTION_ARGS) {
  uint8_t arg1 = PG_GETARG_INT8(0);
  PG_RETURN_CSTRING(uint8_t_strings[arg1]);
}