#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <libgen.h> // for basename()
#include "csv.h" // libcsv

/** Print help and exit. */
static void
help(char* name)
{
  fprintf(stderr, "usage: %s [-dhqv] textfile annotationfile\n\n", basename(name));
  fputs("-d      show debug logging messages\n", stderr);
  fputs("-h      print this help and exit\n", stderr);
  fputs("-q      quiet logging (errors only)\n", stderr);
  fputs("-v      verbose logging (default: warnings)\n", stderr);
  exit(0);
}

/** Default log handler - does nothing. */
static void
silent_handler(
    const gchar* log_domain,
    GLogLevelFlags log_level,
    const gchar* message,
    gpointer user_data)
{
  return;
}

/** This log handler prints to STDERR. */
static void
stderr_handler(
    const gchar *log_domain,
    GLogLevelFlags log_level,
    const gchar *message,
    gpointer user_data)
{
  char buffer[20];
  time_t now = time(NULL);
  struct tm *now_p = localtime(&now);
  char *time_p = &buffer[0];
  char *level_p;

  switch (log_level & G_LOG_LEVEL_MASK)
  {
    case G_LOG_LEVEL_ERROR:    level_p = "ERRO"; break;
    case G_LOG_LEVEL_CRITICAL: level_p = "CRIT"; break;
    case G_LOG_LEVEL_WARNING:  level_p = "WARN"; break;
    case G_LOG_LEVEL_MESSAGE:  level_p = "MESG"; break;
    case G_LOG_LEVEL_INFO:     level_p = "INFO"; break;
    case G_LOG_LEVEL_DEBUG:    level_p = "DEBG"; break;
    default:                   level_p = "LVL?";
  }

  if (strftime(time_p, 20, "%Y-%M-%d %H:%M:%S", now_p) == 0)
    time_p = asctime(now_p);

  fprintf(stderr, "%s %s: %s: %s\n", level_p, time_p, log_domain, message);
}

typedef struct
{
  char section;
  int start;
  int end;
} offset;

/** The single data structure used for processing all input. */
typedef struct
{
  bool flag;
  long id;
  char *title;
  int *toffsets;
  char *abstract;
  int *aoffsets;
  GSList *offset_list;
  char *quote;
  char *class;
  char section;
  int start;
  int end;
  FILE *ann_file;
  char *ann_line;
  struct csv_parser *ann_parser;
} citation;

/** Calculate the number of characters in a UTF-8 string. */
int
utf8strlen(char* utf8)
{
  int i = -1, j = 0;
  while (utf8[++i])
    if ((utf8[i] & 0xC0) != 0x80) j++;
  return j;
}

/** Create a byte-offset array for each character in a UTF-8 string. */
int*
utf8offsets(char* utf8)
{
  int *offsets = malloc(sizeof(int) * (utf8strlen(utf8) + 1));
  int i = -1, j = 0;
  while (utf8[++i])
    if ((utf8[i] & 0xC0) != 0x80) offsets[j++] = i;
  offsets[j] = i;
  return offsets;
}

/** Trim leading and trailing spaces "in place". */
char*
trim(char *str)
{
  char *end;
  // Trim leading space
  while (isspace(*str)) str++;
  // All spaces?
  if (*str == 0) return str;
  // Trim trailing space
  end = str + strlen(str) - 1;
  while (end > str && isspace(*end)) end--;
  // Write new null terminator
  *(end+1) = 0;
  return str;
}

/**
 * Return true if the citation is correct.
 *
 * Note that this method might alter the offsets of annotations.
 */
bool
check(citation *c) {
  int i, idx;
  char *text = c->title;
  int *offsets = c->toffsets;
  char *quote = c->quote;
  int qlen = utf8strlen(quote), tlen;
  GSList *item = g_slist_next(c->offset_list);

  /* select relevant text section */
  if (c->section == 'A')
  {
    text = c->abstract;
    offsets = c->aoffsets;
  }

  /* trim whitespaces */
  for (i = c->start; i < c->end; i++)
  {
    idx = offsets[i];
    if (!isspace(text[idx])) break;
    g_debug(
        "trimming whitespace prefix in %c %i:%i '%s' on %li at (%i->%i)",
        c->section, c->start, c->end, c->quote, c->id, i, idx
    ); 
  }
  // ALTER OFFSETS IF NECESSARY //
  c->start = i;
  for (i = c->end - 1; i >= c->start; i--)
  {
    idx = offsets[i];
    if (!isspace(text[idx])) break;
    g_debug(
        "trimming whitespace suffix in %c %i:%i '%s' on %li at (%i->%i)",
        c->section, c->start, c->end, c->quote, c->id, i, idx
    ); 
  }
  // ALTER OFFSETS IF NECESSARY //
  c->end = i + 1;

  /* compare length of annotation to length of quoted string */
  tlen = c->end - c->start; 
  if (qlen != tlen)
  {
    g_warning(
        "%c %i:%i '%s' on %li length %i != %i ('%s')",
        c->section, c->start, c->end, c->quote, c->id, qlen, tlen, text
    );
    return false;
  }

  /* compare each byte of the quoted string and the annotation */
  idx = offsets[c->start];
  qlen = strlen(quote);
  for (i = 0; i < qlen; ++i)
  {
    if (quote[i] != text[idx + i]) {
      g_warning(
          "%c %i:%i '%s' on %li mismatch at %i (0x%x != 0x%x)",
          c->section, c->start, c->end, c->quote, c->id, i, quote[i], text[idx+i]
      );
      return false;
    }
  }

  /* ensure all annoations on this article are non-overlapping */
  while (item)
  {
    offset *off = (offset*) item->data;
    if (off->section == c->section)
    {
      g_debug("comparing %c %i:%i and %c %i:%i",
          c->section, c->start, c->end, off->section, off->start, off->end 
      );
      if (c->start == off->start && c->end == off->end)
      {
          g_message(
              "skipping duplicate of %c %i:%i '%s' on %li",
              c->section, c->start, c->end, c->quote, c->id
          );
          return false;
      }
      else if (c->start <= off->start && c->end >= off->start)
      {
          g_warning(
              "head of %c %i:%i '%s' on %li overlaps with %i:%i",
              c->section, c->start, c->end, c->quote, c->id, off->start, off->end 
          );
          return false;
      }
      else if (c->start <= off->end && c->end >= off->end)
      {
          g_warning(
              "tail of %c %i:%i '%s' on %li overlaps with %i:%i",
              c->section, c->start, c->end, c->quote, c->id, off->start, off->end 
          );
          return false;
      }
      else if (c->start >= off->start && c->end <= off->end)
      {
          g_warning(
              "body of %c %i:%i '%s' on %li overlaps with %i:%i",
              c->section, c->start, c->end, c->quote, c->id, off->start, off->end 
          );
          return false;
      }
    }
    item = g_slist_next(item);
  }

  return true;
}

void
annrow(int chr, void *data)
{
  citation *c = (citation *) data;
  g_debug("checking %c %i:%i '%s' on %li", c->section, c->start, c->end, c->quote, c->id);
  if (check(c))
    printf(
        "%li\t%c\t%i\t%i\t%s\t%s\n",
        c->id, c->section, c->start, c->end, c->quote, c->class
    );
  offset *off = (offset*) malloc(sizeof(offset));
  off->section = c->section;
  off->start = c->start;
  off->end = c->end;
  c->offset_list = g_slist_append(c->offset_list, off);
  free(c->quote);
  c->quote = NULL;
  free(c->class);
  c->class = NULL;
  c->section = 0;
  c->start = -1;
  c->end = -1;
}

void
anncol(void *val, size_t len, void *data)
{
  citation *c = (citation *) data;
  char *string = malloc(len+1);

  string[len] = 0;
  strncpy(string, val, len);
  //g_debug("anncol %li '%s'", c->id, string);

  if (atol(string) == c->id)
  {
    //g_debug("parsing annotation for %s", string);
  }
  else if (!c->section)
  {
    string = trim(string);

    if (len == 1 && (string[0] != 'A' || string[0] != 'T'))
    {
      c->section = string[0];
    }
    else
    {
      g_error("illegal section '%s' for %li", string, c->id);
    }
    free(string);
  }
  else if (c->start == -1)
  {
    c->start = atoi(string);
    if (!c->start && (len > 1 || string[0] != '0')) {
      g_message("start '%s' invalid", string);
      c->start = -2;
    }
    free(string);
  }
  else if (c->end == -1)
  {
    c->end = atoi(string);
    if (!c->end && (len > 1 || string[0] != '0')) {
      g_message("end '%s' invalid", string);
      c->end = -2;
    }
    free(string);
  }
  else if (!c->quote)
  {
    c->quote = trim(string);
  }
  else if (!c->class)
  {
    c->class = trim(string);
  }
  else
  {
    g_error("unknown annotation value '%s'", string);
    free(string);
  }
}

void
txtcol(void *val, size_t len, void *data)
{
  citation *c = (citation *) data;
  char *string = malloc(len+1);

  string[len] = 0;
  strncpy(string, val, len);

  if (!c->flag)
  {
    c->flag = true;
    c->id = 0L;
    if (c->title)
      free(c->title);
    if (c->toffsets)
      free(c->toffsets);
    c->title = NULL;
    c->toffsets = NULL;
    if (c->abstract)
      free(c->abstract);
    if (c->aoffsets)
      free(c->aoffsets);
    c->abstract = NULL;
    c->aoffsets = NULL;
    c->start = -1;
    c->end = -1;
  }
  if (!c->id) {
    c->id = atol(string);
    if (c->id == 0L)
      g_critical("could not parse id '%s'", string);
    free(string);
  }
  else if (!c->title)
  {
    c->title = string;
  }
  else if (!c->abstract)
  {
    c->abstract = string;
  }
  else
  {
    g_error("unknown text field '%s'", string);
  }
}

void
txtrow(int chr, void *data)
{
  citation *c = (citation *) data;
  size_t chars = 0;
  ssize_t bytes = 0;
  char *line = NULL;
  c->offset_list = NULL;

  if (c->ann_line) {
    line = c->ann_line;
    bytes = strlen(line);
    g_debug("fetching stored annotion '%s'", line);
  } else {
    bytes = getline(&line, &chars, c->ann_file);
    g_debug("reading new annotion '%s'", line);
  }
  c->aoffsets = utf8offsets(c->abstract);
  c->toffsets = utf8offsets(c->title);

  while (bytes != -1 && line && atol(line) == c->id) {
    if (csv_parse(c->ann_parser, line, bytes, anncol, annrow, c) != bytes) {
      g_critical("parsing annotation CSV: %s", csv_strerror(csv_error(c->ann_parser)));
      csv_free(c->ann_parser);
      break;
    }
    bytes = getline(&line, &chars, c->ann_file);
    g_debug("reading next annotation '%s'", line);
  }
  c->ann_line = line;
  g_debug("storing annotion '%s'", line);
  g_debug("checked %i annotations for %li", g_slist_length(c->offset_list), c->id);
  g_slist_free_full(c->offset_list, &free);
  c->flag = false;
}

int
run(FILE *txt_file, FILE *ann_file)
{
  char buf[1024];
  size_t bytes_read;
  citation c = {};
  struct csv_parser txt_parser;
  struct csv_parser ann_parser;
  int err = csv_init(&txt_parser, 0);

  if (err)
  {
    g_critical("libcsv parser setup failed (%i)", err);
    return EXIT_FAILURE;
  }
  err = csv_init(&ann_parser, 0);
  if (err)
  {
    csv_free(&txt_parser);
    g_critical("libcsv parser setup failed (%i)", err);
    return EXIT_FAILURE;
  }

  csv_set_delim(&txt_parser, '\t');
  csv_set_delim(&ann_parser, '\t');
  c.ann_file = ann_file;
  c.ann_parser = &ann_parser;

  while ((bytes_read = fread(buf, 1, 1024, txt_file)) > 0) {
    if (csv_parse(&txt_parser, buf, bytes_read, txtcol, txtrow, &c) != bytes_read) {
      g_error("parsing text CSV: %s", csv_strerror(csv_error(&txt_parser)));
      csv_free(&txt_parser);
      csv_free(&ann_parser);
      return EXIT_FAILURE;
    }
  }

  csv_fini(&txt_parser, txtcol, txtrow, &c); 
  csv_fini(&ann_parser, anncol, annrow, &c); 
  csv_free(&txt_parser);
  csv_free(&ann_parser);
  return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
  int verbosity = 1;
  int show_help = 0;
  int c;
  
  /* default logging: silence */
  g_log_set_default_handler(silent_handler, NULL);

  /* option parsing */
  while ((c = getopt(argc, argv, "hqvd")) != -1)
    switch (c)
    {
      case 'h': show_help = 1; break;
      case 'd': if (verbosity == 1) verbosity = 3; break;
      case 'v': if (verbosity == 1) verbosity = 2; break;
      case 'q': if (verbosity == 1) verbosity = 0; break;
      case '?': break; // getopt prints an error message
      default: abort();
    }

  /* logging setup */
  GLogLevelFlags log_level = G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR | G_LOG_FLAG_FATAL;
  if (verbosity > 0) log_level |= G_LOG_LEVEL_WARNING;
  if (verbosity > 1) log_level |= G_LOG_LEVEL_MESSAGE;
  if (verbosity > 2) log_level |= G_LOG_LEVEL_INFO | G_LOG_LEVEL_DEBUG;
  g_log_set_handler(G_LOG_DOMAIN, log_level, stderr_handler, NULL);

  /* print help and exit if requested */
  if (show_help) help (argv[0]);

  /* exit if the setup of any component fails */
  if (argc - optind != 2)
  {
    g_critical("wrong number of arguments (%i/2)", argc - optind);
    exit(EXIT_FAILURE);
  }
  g_message("text file: '%s'", argv[optind]);
  FILE *text_file = fopen(argv[optind], "rb");
  if (!text_file)
  {
    g_critical("could not read text file '%s'", argv[optind]);
    exit(EXIT_FAILURE);
  }
  g_message("annotation file: '%s'", argv[optind+1]);
  FILE *ann_file = fopen(argv[optind+1], "rb");
  if (!ann_file)
  {
    fclose(text_file);
    g_critical("could not read annotation file '%s'", argv[optind+1]);
    exit(EXIT_FAILURE);
  }
 
  /* RUN PROGRAM */
  int exit_val = run(text_file, ann_file);

  /* cleanup open handles */
  fclose(text_file);
  fclose(ann_file);
  g_message("check complete");
  return exit_val;
}
