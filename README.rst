chemcheck
=========

a syntax checker for BioCreative IV CHEMDNER_ task annotations

Synopsis
--------

A tool to ensure a CHEMDNER_ annotation file is syntactically correct::

  chemcheck sorted_text.tsv sorted_annotations.tsv > pruned_annotations.tsv

Requirements
------------

To compile this library, glib and libcsv need to be installed and visible via
pkg-config::

  make chemcheck

Usage
-----

Before using chemcheck, ensure that your data is sorted::

  sort input > sorted_input

chemcheck reads the text file one article per row (containing an ID, title
and abstract column), while at the same time checking the annotations for the
given ID in the annotation file. (This is why the input has to be sorted;
Actually, only the articles and corresponding annotations have to appear in
the same order.) The annotation file is expected to contain the following
columns: ID, section ('A' or 'T'), start offset, end offset, annotated string,
class. Note that the offsets are character offsets, not byte offsets, while
all strings should be encoded with UTF-8::

  chemcheck sorted_text.tsv sorted_annotations.tsv > checked_annotations.tsv

chemcheck strips leading and trailing whitespaces on both the annotated string
as well as the offsets. Then, it checks if the annotated string and the
offsets have matching lenghts. Third, it ensures that the annotated string and
the string found at the given character (not byte!) offset in the text file
match. Last, a check is made to ensure the annotation does not (partially or
fully) overlap with any other annotation.

If all these checks pass, the (possibly whitespace-corrected) annotation is
printed to STDOUT; If not, a warning (or simple message in case of duplicates)
is printed to STDERR and the annotation is not printed.

.. _CHEMDNER: http://www.biocreative.org/tasks/biocreative-iv/chemdner/
