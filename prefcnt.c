/* 
  Copyright (C) 2006 Mandriva Conectiva S.A.
  Copyright (C) 2006 Arnaldo Carvalho de Melo <acme@mandriva.com>

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.
*/

#include <assert.h>
#include <dwarf.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "classes.h"

static struct option long_options[] = {
	{ "help",			no_argument,		NULL, 'h' },
	{ NULL, 0, NULL, 0, }
};

static void usage(void)
{
	fprintf(stderr,
		"usage: prefcnt [options] <file_name>\n"
		" where: \n"
		"   -h, --help   usage options\n");
}

static void refcnt_class(struct class *class);

static void refcnt_member(struct class_member *member)
{
	if (member->visited)
		return;
	member->visited = 1;
	if (member->type != 0) { /* if not void */
		struct class *type = cu__find_class_by_id(member->class->cu,
							  member->type);
		if (type != NULL)
			refcnt_class(type);
	}
}

static void refcnt_parameter(const struct class_member *member)
{
	if (member->type != 0) { /* if not void */
		struct class *type = cu__find_class_by_id(member->class->cu,
							  member->type);

		if (type != NULL)
			refcnt_class(type);
	}
}

static void refcnt_variable(const struct variable *variable)
{
	if (variable->type != 0) { /* if not void */
		struct class *type = cu__find_class_by_id(variable->cu,
							  variable->type);
		if (type != NULL)
			refcnt_class(type);
	}
}

static void refcnt_inline_expansion(const struct inline_expansion *exp)
{
	if (exp->type != 0) { /* if not void */
		struct class *type = cu__find_class_by_id(exp->class->cu,
							  exp->type);
		assert(type != NULL);
		refcnt_class(type);
	}
}

static void refcnt_class(struct class *class)
{
	struct class_member *member;
	struct inline_expansion *exp;
	struct variable *variable;

	class->refcnt++;

	if (class->type != 0) /* if not void */ {
		struct class *type = cu__find_class_by_id(class->cu, class->type);

		if (type != NULL)
			refcnt_class(type);
	}

	if (class->tag == DW_TAG_structure_type ||
	    class->tag == DW_TAG_union_type) {
		list_for_each_entry(member, &class->members, node)
			refcnt_member(member);
	} else if (class->tag == DW_TAG_subprogram) {
		list_for_each_entry(member, &class->members, node)
			refcnt_parameter(member);
	}

	list_for_each_entry(variable, &class->variables, class_node)
		refcnt_variable(variable);

	list_for_each_entry(exp, &class->inline_expansions, node)
		refcnt_inline_expansion(exp);
}

static void refcnt_function(struct class *function)
{
	assert(function->tag == DW_TAG_subprogram);
	/*
	 * We're not interested in inlines at this point, if we
	 * have inline expansions we'll refcount the inline
	 * definition and crawl into its return type and
	 * parameters.
	 */
	if (function->inlined)
		return;

	refcnt_class(function);
}

static int refcnt_iterator(struct class *class, void *cookie)
{
	switch (class->tag) {
	case DW_TAG_structure_type:
		class__find_holes(class);
		break;
	case DW_TAG_subprogram:
		refcnt_function(class);
		break;
	}

	return 0;
}

static int cu_refcnt_iterator(struct cu *cu, void *cookie)
{
	return cu__for_each_class(cu, refcnt_iterator, cookie);
}

static int lost_iterator(struct class *class, void *cookie)
{
	if (class->refcnt == 0 && class->decl_file != NULL)
		class__print(class);
	return 0;
}

static int cu_lost_iterator(struct cu *cu, void *cookie)
{
	return cu__for_each_class(cu, lost_iterator, cookie);
}

int main(int argc, char *argv[])
{
	int option, option_index;
	struct cus *cus;
	const char *file_name;

	while ((option = getopt_long(argc, argv, "h",
				     long_options, &option_index)) >= 0)
		switch (option) {
		case 'h': usage(); return EXIT_SUCCESS;
		default:  usage(); return EXIT_FAILURE;
		}

	if (optind < argc) {
		switch (argc - optind) {
		case 1:	 file_name = argv[optind++];	 break;
		default: usage();			 return EXIT_FAILURE;
		}
	} else {
		usage();
		return EXIT_FAILURE;
	}

	cus = cus__new(file_name);
	if (cus == NULL) {
		fputs("prefcnt: insufficient memory\n", stderr);
		return EXIT_FAILURE;
	}

	if (cus__load(cus) != 0) {
		fprintf(stderr, "prefcnt: couldn't load DWARF info from %s\n",
			file_name);
		return EXIT_FAILURE;
	}

	cus__for_each_cu(cus, cu_refcnt_iterator, NULL);
	cus__for_each_cu(cus, cu_lost_iterator, NULL);

	return EXIT_SUCCESS;
}
