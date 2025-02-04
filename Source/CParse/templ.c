/* ----------------------------------------------------------------------------- 
 * This file is part of SWIG, which is licensed as a whole under version 3 
 * (or any later version) of the GNU General Public License. Some additional
 * terms also apply to certain portions of SWIG. The full details of the SWIG
 * license and copyrights can be found in the LICENSE and COPYRIGHT files
 * included with the SWIG source code as distributed by the SWIG developers
 * and at https://www.swig.org/legal.html.
 *
 * templ.c
 *
 * Expands a template into a specialized version.   
 * ----------------------------------------------------------------------------- */

#include "swig.h"
#include "cparse.h"

static int template_debug = 0;


const char *baselists[3];

void SwigType_template_init(void) {
  baselists[0] = "baselist";
  baselists[1] = "protectedbaselist";
  baselists[2] = "privatebaselist";
}

void Swig_cparse_debug_templates(int x) {
  template_debug = x;
}

/* -----------------------------------------------------------------------------
 * add_parms()
 *
 * Add the value and type of each parameter into patchlist and typelist
 * (List of String/SwigType) for later template parameter substitutions.
 * ----------------------------------------------------------------------------- */

static void add_parms(ParmList *p, List *patchlist, List *typelist, int is_pattern) {
  while (p) {
    SwigType *ty = Getattr(p, "type");
    SwigType *val = Getattr(p, "value");
    Append(typelist, ty);
    Append(typelist, val);
    if (is_pattern) {
      /* Typemap patterns are not simple parameter lists.
       * Output style ("out", "ret" etc) typemap names can be
       * qualified names and so may need template expansion */
      SwigType *name = Getattr(p, "name");
      Append(typelist, name);
    }
    Append(patchlist, val);
    p = nextSibling(p);
  }
}

/* -----------------------------------------------------------------------------
 * expand_variadic_parms()
 *
 * Expand variadic parameter in the parameter list stored as attribute in n. For example:
 *   template <typename... T> struct X : { X(T&... tt); }
 *   %template(XABC) X<A,B,C>;
 * inputs for the constructor parameter list will be for attribute = "parms":
 *   Getattr(n, attribute)   : v.r.T tt
 *   unexpanded_variadic_parm: v.typename T
 *   expanded_variadic_parms : A,B,C
 * results in:
 *   Getattr(n, attribute)   : r.A,r.B,r.C
 * that is, template is expanded as: struct XABC : { X(A&,B&,C&); }
 * Note that there are no parameter names are in the expanded parameter list.
 * Nothing happens if the parameter list has no variadic parameters.
 * ----------------------------------------------------------------------------- */

static void expand_variadic_parms(Node *n, const char *attribute, Parm *unexpanded_variadic_parm, ParmList *expanded_variadic_parms) {
  ParmList *p = Getattr(n, attribute);
  if (unexpanded_variadic_parm) {
    Parm *variadic = ParmList_variadic_parm(p);
    if (variadic) {
      SwigType *type = Getattr(variadic, "type");
      String *unexpanded_name = Getattr(unexpanded_variadic_parm, "name");
      ParmList *expanded = CopyParmList(expanded_variadic_parms);
      Parm *ep = expanded;
      while (ep) {
	SwigType *newtype = Copy(type);
	SwigType_del_variadic(newtype);
	Replaceid(newtype, unexpanded_name, Getattr(ep, "type"));
	Setattr(ep, "type", newtype);
	ep = nextSibling(ep);
      }
      expanded = ParmList_replace_last(p, expanded);
      Setattr(n, attribute, expanded);
    }
  }
}

/* -----------------------------------------------------------------------------
 * expand_parms()
 *
 * Expand variadic parameters in parameter lists and add parameters to patchlist
 * and typelist for later template parameter substitutions.
 * ----------------------------------------------------------------------------- */

static void expand_parms(Node *n, const char *attribute, Parm *unexpanded_variadic_parm, ParmList *expanded_variadic_parms, List *patchlist, List *typelist, int is_pattern) {
  ParmList *p;
  expand_variadic_parms(n, attribute, unexpanded_variadic_parm, expanded_variadic_parms);
  p = Getattr(n, attribute);
  add_parms(p, patchlist, typelist, is_pattern);
}

/* -----------------------------------------------------------------------------
 * cparse_template_expand()
 *
 * Expands a template node into a specialized version.  This is done by
 * patching typenames and other aspects of the node according to a list of
 * template parameters
 * ----------------------------------------------------------------------------- */

static void cparse_template_expand(Node *templnode, Node *n, String *tname, String *rname, String *templateargs, List *patchlist, List *typelist, List *cpatchlist, Parm *unexpanded_variadic_parm, ParmList *expanded_variadic_parms) {
  static int expanded = 0;
  String *nodeType;
  if (!n)
    return;
  nodeType = nodeType(n);
  if (Getattr(n, "error"))
    return;

  if (Equal(nodeType, "template")) {
    /* Change the node type back to normal */
    if (!expanded) {
      expanded = 1;
      set_nodeType(n, Getattr(n, "templatetype"));
      cparse_template_expand(templnode, n, tname, rname, templateargs, patchlist, typelist, cpatchlist, unexpanded_variadic_parm, expanded_variadic_parms);
      expanded = 0;
      return;
    } else {
      /* Called when template appears inside another template */
      /* Member templates */

      set_nodeType(n, Getattr(n, "templatetype"));
      cparse_template_expand(templnode, n, tname, rname, templateargs, patchlist, typelist, cpatchlist, unexpanded_variadic_parm, expanded_variadic_parms);
      set_nodeType(n, "template");
      return;
    }
  } else if (Equal(nodeType, "cdecl")) {
    /* A simple C declaration */
    SwigType *t, *v, *d;
    String *code;
    t = Getattr(n, "type");
    v = Getattr(n, "value");
    d = Getattr(n, "decl");

    code = Getattr(n, "code");

    Append(typelist, t);
    Append(typelist, d);
    Append(patchlist, v);
    Append(cpatchlist, code);

    if (Getattr(n, "conversion_operator")) {
      Append(cpatchlist, Getattr(n, "name"));
      if (Getattr(n, "sym:name")) {
	Append(cpatchlist, Getattr(n, "sym:name"));
      }
    }
    if (checkAttribute(n, "storage", "friend")) {
      String *symname = Getattr(n, "sym:name");
      if (symname) {
	String *stripped_name = SwigType_templateprefix(symname);
	Setattr(n, "sym:name", stripped_name);
	Delete(stripped_name);
      }
      Append(typelist, Getattr(n, "name"));
    }

    expand_parms(n, "parms", unexpanded_variadic_parm, expanded_variadic_parms, cpatchlist, typelist, 0);
    expand_parms(n, "throws", unexpanded_variadic_parm, expanded_variadic_parms, cpatchlist, typelist, 0);

  } else if (Equal(nodeType, "class")) {
    /* Patch base classes */
    {
      int b = 0;
      for (b = 0; b < 3; ++b) {
	List *bases = Getattr(n, baselists[b]);
	if (bases) {
	  int i;
	  int ilen = Len(bases);
	  for (i = 0; i < ilen; i++) {
	    String *name = Copy(Getitem(bases, i));
	    if (SwigType_isvariadic(name)) {
	      Parm *parm = NewParmWithoutFileLineInfo(name, 0);
	      Node *temp_parm_node = NewHash();
	      Setattr(temp_parm_node, "variadicbaseparms", parm);
	      assert(i == ilen - 1);
	      Delitem(bases, i);
	      expand_variadic_parms(temp_parm_node, "variadicbaseparms", unexpanded_variadic_parm, expanded_variadic_parms);
	      {
		Parm *vp = Getattr(temp_parm_node, "variadicbaseparms");
		while (vp) {
		  String *name = Copy(Getattr(vp, "type"));
		  Append(bases, name);
		  Append(typelist, name);
		  vp = nextSibling(vp);
		}
	      }
	      Delete(temp_parm_node);
	    } else {
	      Setitem(bases, i, name);
	      Append(typelist, name);
	    }
	  }
	}
      }
    }
    /* Patch children */
    {
      Node *cn = firstChild(n);
      while (cn) {
	cparse_template_expand(templnode, cn, tname, rname, templateargs, patchlist, typelist, cpatchlist, unexpanded_variadic_parm, expanded_variadic_parms);
	cn = nextSibling(cn);
      }
    }
  } else if (Equal(nodeType, "constructor")) {
    String *name = Getattr(n, "name");
    if (!(Getattr(n, "templatetype"))) {
      String *symname;
      String *stripped_name = SwigType_templateprefix(name);
      if (Strstr(tname, stripped_name)) {
	Replaceid(name, stripped_name, tname);
      }
      Delete(stripped_name);
      symname = Getattr(n, "sym:name");
      if (symname) {
	stripped_name = SwigType_templateprefix(symname);
	if (Strstr(tname, stripped_name)) {
	  Replaceid(symname, stripped_name, tname);
	}
	Delete(stripped_name);
      }
      if (strchr(Char(name), '<')) {
	Append(patchlist, Getattr(n, "name"));
      } else {
	Append(name, templateargs);
      }
      name = Getattr(n, "sym:name");
      if (name) {
	if (strchr(Char(name), '<')) {
	  Clear(name);
	  Append(name, rname);
	} else {
	  String *tmp = Copy(name);
	  Replace(tmp, tname, rname, DOH_REPLACE_ANY);
	  Clear(name);
	  Append(name, tmp);
	  Delete(tmp);
	}
      }
    }
    Append(cpatchlist, Getattr(n, "code"));
    Append(typelist, Getattr(n, "decl"));
    expand_parms(n, "parms", unexpanded_variadic_parm, expanded_variadic_parms, cpatchlist, typelist, 0);
    expand_parms(n, "throws", unexpanded_variadic_parm, expanded_variadic_parms, cpatchlist, typelist, 0);
  } else if (Equal(nodeType, "destructor")) {
    /* We only need to patch the dtor of the template itself, not the destructors of any nested classes, so check that the parent of this node is the root
     * template node, with the special exception for %extend which adds its methods under an intermediate node. */
    Node* parent = parentNode(n);
    if (parent == templnode || (parentNode(parent) == templnode && Equal(nodeType(parent), "extend"))) {
      String *name = Getattr(n, "name");
      if (name) {
	if (strchr(Char(name), '<'))
	  Append(patchlist, Getattr(n, "name"));
	else
	  Append(name, templateargs);
      }
      name = Getattr(n, "sym:name");
      if (name) {
	if (strchr(Char(name), '<')) {
	  String *sn = Copy(tname);
	  Setattr(n, "sym:name", sn);
	  Delete(sn);
	} else {
	  Replace(name, tname, rname, DOH_REPLACE_ANY);
	}
      }
      Append(cpatchlist, Getattr(n, "code"));
    }
  } else if (Equal(nodeType, "using")) {
    String *uname = Getattr(n, "uname");
    if (uname && strchr(Char(uname), '<')) {
      Append(patchlist, uname);
    }
    if (Getattr(n, "namespace")) {
      /* Namespace link.   This is nasty.  Is other namespace defined? */

    }
  } else {
    /* Look for obvious parameters */
    Node *cn;
    Append(cpatchlist, Getattr(n, "code"));
    Append(typelist, Getattr(n, "type"));
    Append(typelist, Getattr(n, "decl"));
    expand_parms(n, "parms", unexpanded_variadic_parm, expanded_variadic_parms, cpatchlist, typelist, 0);
    expand_parms(n, "kwargs", unexpanded_variadic_parm, expanded_variadic_parms, cpatchlist, typelist, 0);
    expand_parms(n, "pattern", unexpanded_variadic_parm, expanded_variadic_parms, cpatchlist, typelist, 1);
    expand_parms(n, "throws", unexpanded_variadic_parm, expanded_variadic_parms, cpatchlist, typelist, 0);
    cn = firstChild(n);
    while (cn) {
      cparse_template_expand(templnode, cn, tname, rname, templateargs, patchlist, typelist, cpatchlist, unexpanded_variadic_parm, expanded_variadic_parms);
      cn = nextSibling(cn);
    }
  }
}

/* -----------------------------------------------------------------------------
 * cparse_fix_function_decl()
 *
 * Move the prefix of the "type" attribute (excluding any trailing qualifier)
 * to the end of the "decl" attribute.
 * Examples:
 *   decl="f().", type="p.q(const).char"  => decl="f().p.", type="q(const).char"
 *   decl="f().p.", type="p.SomeClass"    => decl="f().p.p.", type="SomeClass"
 *   decl="f().", type="r.q(const).p.int" => decl="f().r.q(const).p.", type="int"
 * ----------------------------------------------------------------------------- */

static void cparse_fix_function_decl(String *name, SwigType *decl, SwigType *type) {
  String *prefix;
  int prefixLen;
  SwigType *last;

  /* The type's prefix is what potentially has to be moved to the end of 'decl' */
  prefix = SwigType_prefix(type);

  /* First some parts (qualifier and array) have to be removed from prefix
     in order to remain in the 'type' attribute. */
  last = SwigType_last(prefix);
  while (last) {
    if (SwigType_isqualifier(last) || SwigType_isarray(last)) {
      /* Keep this part in the 'type' */
      Delslice(prefix, Len(prefix) - Len(last), DOH_END);
      Delete(last);
      last = SwigType_last(prefix);
    } else {
      /* Done with processing prefix */
      Delete(last);
      last = 0;
    }
  }

  /* Transfer prefix from the 'type' to the 'decl' attribute */
  prefixLen = Len(prefix);
  if (prefixLen > 0) {
    Append(decl, prefix);
    Delslice(type, 0, prefixLen);
    if (template_debug) {
      Printf(stdout, "    change function '%s' to type='%s', decl='%s'\n", name, type, decl);
    }
  }

  Delete(prefix);
}

/* -----------------------------------------------------------------------------
 * cparse_postprocess_expanded_template()
 *
 * This function postprocesses the given node after template expansion.
 * Currently the only task to perform is fixing function decl and type attributes.
 * ----------------------------------------------------------------------------- */

static void cparse_postprocess_expanded_template(Node *n) {
  String *nodeType;
  if (!n)
    return;
  nodeType = nodeType(n);
  if (Getattr(n, "error"))
    return;

  if (Equal(nodeType, "cdecl")) {
    /* A simple C declaration */
    SwigType *d = Getattr(n, "decl");
    if (d && SwigType_isfunction(d)) {
      /* A function node */
      SwigType *t = Getattr(n, "type");
      if (t) {
	String *name = Getattr(n, "name");
	cparse_fix_function_decl(name, d, t);
      }
    }
  } else {
    /* Look for any children */
    Node *cn = firstChild(n);
    while (cn) {
      cparse_postprocess_expanded_template(cn);
      cn = nextSibling(cn);
    }
  }
}

/* -----------------------------------------------------------------------------
 * partial_arg()
 * ----------------------------------------------------------------------------- */

static String *partial_arg(String *s, String *p) {
  char *c;
  char *cp = Char(p);
  String *prefix;
  String *newarg;

  /* Find the prefix on the partial argument */

  c = strchr(cp, '$');
  if (!c) {
    return Copy(s);
  }
  prefix = NewStringWithSize(cp, (int)(c - cp));
  newarg = Copy(s);
  Replace(newarg, prefix, "", DOH_REPLACE_FIRST);
  Delete(prefix);
  return newarg;
}

/* -----------------------------------------------------------------------------
 * Swig_cparse_template_expand()
 * ----------------------------------------------------------------------------- */

int Swig_cparse_template_expand(Node *n, String *rname, ParmList *tparms, Symtab *tscope) {
  List *patchlist, *cpatchlist, *typelist;
  String *templateargs;
  String *tname;
  String *iname;
  String *tbase;
  Parm *unexpanded_variadic_parm = 0;
  ParmList *expanded_variadic_parms = 0;
  patchlist = NewList();  /* List of String * ("name" and "value" attributes) */
  cpatchlist = NewList(); /* List of String * (code) */
  typelist = NewList();   /* List of SwigType * types */

  templateargs = NewStringEmpty();
  SwigType_add_template(templateargs, tparms);

  tname = Copy(Getattr(n, "name"));
  tbase = Swig_scopename_last(tname);

  /* Look for partial specialization matching */
  if (Getattr(n, "partialargs")) {
    Parm *p, *tp;
    ParmList *ptargs = SwigType_function_parms(Getattr(n, "partialargs"), n);
    p = ptargs;
    tp = tparms;
    while (p && tp) {
      SwigType *ptype;
      SwigType *tptype;
      SwigType *partial_type;
      ptype = Getattr(p, "type");
      tptype = Getattr(tp, "type");
      if (ptype && tptype) {
	partial_type = partial_arg(tptype, ptype);
	/*      Printf(stdout,"partial '%s' '%s'  ---> '%s'\n", tptype, ptype, partial_type); */
	Setattr(tp, "type", partial_type);
	Delete(partial_type);
      }
      p = nextSibling(p);
      tp = nextSibling(tp);
    }
    assert(ParmList_len(ptargs) == ParmList_len(tparms));
    Delete(ptargs);
  }

  /*
    Parm *p = tparms;
    while (p) {
      Printf(stdout, "tparm: '%s' '%s' '%s'\n", Getattr(p, "name"), Getattr(p, "type"), Getattr(p, "value"));
      p = nextSibling(p);
    }
  */

  ParmList *templateparms = Getattr(n, "templateparms");
  unexpanded_variadic_parm = ParmList_variadic_parm(templateparms);
  if (unexpanded_variadic_parm)
    expanded_variadic_parms = ParmList_nth_parm(tparms, ParmList_len(templateparms) - 1);

  /*  Printf(stdout,"targs = '%s'\n", templateargs);
     Printf(stdout,"rname = '%s'\n", rname);
     Printf(stdout,"tname = '%s'\n", tname);  */
  cparse_template_expand(n, n, tname, rname, templateargs, patchlist, typelist, cpatchlist, unexpanded_variadic_parm, expanded_variadic_parms);

  /* Set the name */
  {
    String *name = Getattr(n, "name");
    if (name) {
      Append(name, templateargs);
    }
    iname = name;
  }

  /* Patch all of the types */
  {
    Parm *tp = Getattr(n, "templateparms");
    Parm *p = tparms;
    /*    Printf(stdout,"%s\n", ParmList_str_defaultargs(tp)); */

    if (p && tp) {
      Symtab *tsdecl = Getattr(n, "sym:symtab");
      String *tsname = Getattr(n, "sym:name");
      while (p && tp) {
	String *name, *value, *valuestr, *tmp, *tmpr;
	int sz, i;
	String *dvalue = 0;
	String *qvalue = 0;

	name = Getattr(tp, "name");
	value = Getattr(p, "value");

	if (name) {
	  if (!value)
	    value = Getattr(p, "type");
	  qvalue = Swig_symbol_typedef_reduce(value, tsdecl);
	  dvalue = Swig_symbol_type_qualify(qvalue, tsdecl);
	  if (SwigType_istemplate(dvalue)) {
	    String *ty = Swig_symbol_template_deftype(dvalue, tscope);
	    Delete(dvalue);
	    dvalue = ty;
	  }

	  assert(dvalue);
	  valuestr = SwigType_str(dvalue, 0);
	  /* Need to patch default arguments */
	  {
	    Parm *rp = nextSibling(p);
	    while (rp) {
	      String *rvalue = Getattr(rp, "value");
	      if (rvalue) {
		Replace(rvalue, name, dvalue, DOH_REPLACE_ID);
	      }
	      rp = nextSibling(rp);
	    }
	  }
	  sz = Len(patchlist);
	  for (i = 0; i < sz; i++) {
	    String *s = Getitem(patchlist, i);
	    Replace(s, name, dvalue, DOH_REPLACE_ID);
	  }
	  sz = Len(typelist);
	  for (i = 0; i < sz; i++) {
	    SwigType *s = Getitem(typelist, i);

	    assert(!SwigType_isvariadic(s)); /* All parameters should have already been expanded, this is for function that contain variadic parameters only, such as f(v.p.V) */
	    SwigType_variadic_replace(s, unexpanded_variadic_parm, expanded_variadic_parms);

	    /*
	      The approach of 'trivially' replacing template arguments is kind of fragile.
	      In particular if types with similar name in different namespaces appear.
	      We will not replace template args if a type/class exists with the same
	      name which is not a template.
	    */
	    Node *tynode = Swig_symbol_clookup(s, 0);
	    String *tyname  = tynode ? Getattr(tynode, "sym:name") : 0;
	    /*
	    Printf(stdout, "  replacing %s with %s to %s or %s to %s\n", s, name, dvalue, tbase, iname);
	    Printf(stdout, "    %d %s to %s\n", tp == unexpanded_variadic_parm, name, ParmList_str_defaultargs(expanded_variadic_parms));
	    */
	    if (!tyname || !tsname || !Equal(tyname, tsname) || Getattr(tynode, "templatetype")) {
	      SwigType_typename_replace(s, name, dvalue);
	      SwigType_typename_replace(s, tbase, iname);
	    }
	  }

	  tmp = NewStringf("#%s", name);
	  tmpr = NewStringf("\"%s\"", valuestr);

	  sz = Len(cpatchlist);
	  for (i = 0; i < sz; i++) {
	    String *s = Getitem(cpatchlist, i);
	    Replace(s, tmp, tmpr, DOH_REPLACE_ID);
	    Replace(s, name, valuestr, DOH_REPLACE_ID);
	  }
	  Delete(tmp);
	  Delete(tmpr);
	  Delete(valuestr);
	  Delete(dvalue);
	  Delete(qvalue);
	}
	p = nextSibling(p);
	tp = nextSibling(tp);
	if (!p)
	  p = tp;
      }
    } else {
      /* No template parameters at all.  This could be a specialization */
      int i, sz;
      sz = Len(typelist);
      for (i = 0; i < sz; i++) {
	String *s = Getitem(typelist, i);
	assert(!SwigType_isvariadic(s)); /* All parameters should have already been expanded, this is for function that contain variadic parameters only, such as f(v.p.V) */
	SwigType_variadic_replace(s, unexpanded_variadic_parm, expanded_variadic_parms);
	SwigType_typename_replace(s, tbase, iname);
      }
    }
  }
  cparse_postprocess_expanded_template(n);

  /* Patch bases */
  {
    List *bases = Getattr(n, "baselist");
    if (bases) {
      Iterator b;
      for (b = First(bases); b.item; b = Next(b)) {
	String *qn = Swig_symbol_type_qualify(b.item, tscope);
	Clear(b.item);
	Append(b.item, qn);
	Delete(qn);
      }
    }
  }
  Delete(patchlist);
  Delete(cpatchlist);
  Delete(typelist);
  Delete(tbase);
  Delete(tname);
  Delete(templateargs);

  /*  set_nodeType(n,"template"); */
  return 0;
}

typedef enum { ExactNoMatch = -2, PartiallySpecializedNoMatch = -1, PartiallySpecializedMatch = 1, ExactMatch = 2 } EMatch;

/* -----------------------------------------------------------------------------
 * does_parm_match()
 *
 * Template argument deduction - check if a template type matches a partially specialized 
 * template parameter type. Typedef reduce 'partial_parm_type' to see if it matches 'type'.
 *
 * type - template parameter type to match against
 * partial_parm_type - partially specialized template type - a possible match
 * partial_parm_type_base - base type of partial_parm_type
 * tscope - template scope
 * specialization_priority - (output) contains a value indicating how good the match is 
 *   (higher is better) only set if return is set to PartiallySpecializedMatch or ExactMatch.
 * ----------------------------------------------------------------------------- */

static EMatch does_parm_match(SwigType *type, SwigType *partial_parm_type, const char *partial_parm_type_base, Symtab *tscope, int *specialization_priority) {
  static const int EXACT_MATCH_PRIORITY = 99999; /* a number bigger than the length of any conceivable type */
  int matches;
  int substitutions;
  EMatch match;
  SwigType *ty = Swig_symbol_typedef_reduce(type, tscope);
  String *base = SwigType_base(ty);
  SwigType *t = Copy(partial_parm_type);
  substitutions = Replaceid(t, partial_parm_type_base, base); /* eg: Replaceid("p.$1", "$1", "int") returns t="p.int" */
  matches = Equal(ty, t);
  *specialization_priority = -1;
  if (substitutions == 1) {
    /* we have a non-explicit specialized parameter (in partial_parm_type) because a substitution for $1, $2... etc has taken place */
    SwigType *tt = Copy(partial_parm_type);
    int len;
    /*
       check for match to partial specialization type, for example, all of the following could match the type in the %template:
       template <typename T> struct XX {};
       template <typename T> struct XX<T &> {};         // r.$1
       template <typename T> struct XX<T const&> {};    // r.q(const).$1
       template <typename T> struct XX<T *const&> {};   // r.q(const).p.$1
       %template(XXX) XX<int *const&>;                  // r.q(const).p.int

       where type="r.q(const).p.int" will match either of tt="r.", tt="r.q(const)" tt="r.q(const).p"
    */
    Replaceid(tt, partial_parm_type_base, ""); /* remove the $1, $2 etc, eg tt="p.$1" => "p." */
    len = Len(tt);
    if (Strncmp(tt, ty, len) == 0) {
      match = PartiallySpecializedMatch;
      *specialization_priority = len;
    } else {
      match = PartiallySpecializedNoMatch;
    }
    Delete(tt);
  } else {
    match = matches ? ExactMatch : ExactNoMatch;
    if (matches)
      *specialization_priority = EXACT_MATCH_PRIORITY; /* exact matches always take precedence */
  }
  /*
  Printf(stdout, "      does_parm_match %2d %5d [%s] [%s]\n", match, *specialization_priority, type, partial_parm_type);
  */
  Delete(t);
  Delete(base);
  Delete(ty);
  return match;
}

/* -----------------------------------------------------------------------------
 * template_locate()
 *
 * Search for a template that matches name with given parameters.
 * ----------------------------------------------------------------------------- */

static Node *template_locate(String *name, Parm *instantiated_parms, String *symname, Symtab *tscope) {
  Node *n = 0;
  String *tname = 0;
  Node *templ;
  Symtab *primary_scope = 0;
  List *possiblepartials = 0;
  Parm *p;
  Parm *parms = 0;
  Parm *targs;
  ParmList *expandedparms;
  int *priorities_matrix = 0;
  int max_possible_partials = 0;
  int posslen = 0;

  if (template_debug) {
    tname = Copy(name);
    SwigType_add_template(tname, instantiated_parms);
    Printf(stdout, "\n");
    if (symname)
      Swig_diagnostic(cparse_file, cparse_line, "Template debug: Searching for match to: '%s' for instantiation of template named '%s'\n", tname, symname);
    else
      Swig_diagnostic(cparse_file, cparse_line, "Template debug: Searching for match to: '%s' for instantiation of empty template\n", tname);
    Delete(tname);
    tname = 0;
  }

  /* Search for primary (unspecialized) template */
  templ = Swig_symbol_clookup(name, 0);

  if (templ) {
    if (template_debug) {
      Printf(stdout, "    found primary template <%s> '%s'\n", ParmList_str_defaultargs(Getattr(templ, "templateparms")), Getattr(templ, "name"));
    }

    tname = Copy(name);
    parms = CopyParmList(instantiated_parms);

    /* All template specializations must be in the primary template's scope, store the symbol table for this scope for specialization lookups */
    primary_scope = Getattr(templ, "sym:symtab");

    /* Add default values from primary template */
    targs = Getattr(templ, "templateparms");
    expandedparms = Swig_symbol_template_defargs(parms, targs, tscope, primary_scope);

    /* reduce the typedef */
    p = expandedparms;
    while (p) {
      SwigType *ty = Getattr(p, "type");
      if (ty) {
	SwigType *nt = Swig_symbol_type_qualify(ty, tscope);
	Setattr(p, "type", nt);
	Delete(nt);
      }
      p = nextSibling(p);
    }
    SwigType_add_template(tname, expandedparms);

    /* Search for an explicit (exact) specialization. Example: template<> class name<int> { ... } */
    {
      if (template_debug) {
	Printf(stdout, "    searching for : '%s' (explicit specialization)\n", tname);
      }
      n = Swig_symbol_clookup_local(tname, primary_scope);
      if (!n) {
	SwigType *rname = Swig_symbol_typedef_reduce(tname, tscope);
	if (!Equal(rname, tname)) {
	  if (template_debug) {
	    Printf(stdout, "    searching for : '%s' (explicit specialization with typedef reduction)\n", rname);
	  }
	  n = Swig_symbol_clookup_local(rname, primary_scope);
	}
	Delete(rname);
      }
      if (n) {
	Node *tn;
	String *nodeType = nodeType(n);
	if (Equal(nodeType, "template")) {
	  if (template_debug) {
	    Printf(stdout, "    explicit specialization found: '%s'\n", Getattr(n, "name"));
	  }
	  goto success;
	}
	tn = Getattr(n, "template");
	if (tn) {
	  /* Previously wrapped by a template instantiation */
	  Node *previous_named_instantiation = GetFlag(n, "hidden") ? Getattr(n, "csym:nextSibling") : n; /* "hidden" is set when "sym:name" is a __dummy_ name */
	  if (!symname) {
	    /* Quietly ignore empty template instantiations if there is a previous (empty or non-empty) template instantiation */
	    if (template_debug) {
	      if (previous_named_instantiation)
		Printf(stdout, "    previous instantiation with name '%s' found: '%s' - duplicate empty template instantiation ignored\n", Getattr(previous_named_instantiation, "sym:name"), Getattr(n, "name"));
	      else
		Printf(stdout, "    previous empty template instantiation found: '%s' - duplicate empty template instantiation ignored\n", Getattr(n, "name"));
	    }
	    return 0;
	  }
	  /* Accept a second instantiation only if previous template instantiation is empty */
	  if (previous_named_instantiation) {
	    String *previous_name = Getattr(previous_named_instantiation, "name");
	    String *previous_symname = Getattr(previous_named_instantiation, "sym:name");
	    String *unprocessed_tname = Copy(name);
	    SwigType_add_template(unprocessed_tname, instantiated_parms);

	    if (template_debug)
	      Printf(stdout, "    previous instantiation with name '%s' found: '%s' - duplicate instantiation ignored\n", previous_symname, Getattr(n, "name"));
	    SWIG_WARN_NODE_BEGIN(n);
	    Swig_warning(WARN_TYPE_REDEFINED, cparse_file, cparse_line, "Duplicate template instantiation of '%s' with name '%s' ignored,\n", SwigType_namestr(unprocessed_tname), symname);
	    Swig_warning(WARN_TYPE_REDEFINED, Getfile(n), Getline(n), "previous instantiation of '%s' with name '%s'.\n", SwigType_namestr(previous_name), previous_symname);
	    SWIG_WARN_NODE_END(n);

	    Delete(unprocessed_tname);
	    return 0;
	  }
	  if (template_debug)
	    Printf(stdout, "    previous empty template instantiation found: '%s' - using as duplicate instantiation overrides empty template instantiation\n", Getattr(n, "name"));
	  n = tn;
	  goto success;
	}
	Swig_error(cparse_file, cparse_line, "'%s' is not defined as a template. (%s)\n", name, nodeType(n));
	Delete(tname);
	Delete(parms);
	return 0;	  /* Found a match, but it's not a template of any kind. */
      }
    }

    /* Search for partial specializations.
     * Example: template<typename T> class name<T *> { ... } 

     * There are 3 types of template arguments:
     * (1) Template type arguments
     * (2) Template non type arguments
     * (3) Template template arguments
     * only (1) is really supported for partial specializations
     */

    /* Rank each template parameter against the desired template parameters then build a matrix of best matches */
    possiblepartials = NewList();
    {
      char tmp[32];
      List *partials;

      partials = Getattr(templ, "partials"); /* note that these partial specializations do not include explicit specializations */
      if (partials) {
	Iterator pi;
	int parms_len = ParmList_len(parms);
	int *priorities_row;
	max_possible_partials = Len(partials);
	priorities_matrix = (int *)Malloc(sizeof(int) * max_possible_partials * parms_len); /* slightly wasteful allocation for max possible matches */
	priorities_row = priorities_matrix;
	for (pi = First(partials); pi.item; pi = Next(pi)) {
	  Parm *p = parms;
	  int all_parameters_match = 1;
	  int i = 1;
	  Parm *partialparms = Getattr(pi.item, "partialparms");
	  Parm *pp = partialparms;
	  String *templcsymname = Getattr(pi.item, "templcsymname");
	  if (template_debug) {
	    Printf(stdout, "    checking match: '%s' (partial specialization)\n", templcsymname);
	  }
	  if (ParmList_len(partialparms) == parms_len) {
	    while (p && pp) {
	      SwigType *t;
	      sprintf(tmp, "$%d", i);
	      t = Getattr(p, "type");
	      if (!t)
		t = Getattr(p, "value");
	      if (t) {
		EMatch match = does_parm_match(t, Getattr(pp, "type"), tmp, tscope, priorities_row + i - 1);
		if (match < (int)PartiallySpecializedMatch) {
		  all_parameters_match = 0;
		  break;
		}
	      }
	      i++;
	      p = nextSibling(p);
	      pp = nextSibling(pp);
	    }
	    if (all_parameters_match) {
	      Append(possiblepartials, pi.item);
	      priorities_row += parms_len;
	    }
	  }
	}
      }
    }

    posslen = Len(possiblepartials);
    if (template_debug) {
      int i;
      if (posslen == 0)
	Printf(stdout, "    matched partials: NONE\n");
      else if (posslen == 1)
	Printf(stdout, "    chosen partial: '%s'\n", Getattr(Getitem(possiblepartials, 0), "templcsymname"));
      else {
	Printf(stdout, "    possibly matched partials:\n");
	for (i = 0; i < posslen; i++) {
	  Printf(stdout, "      '%s'\n", Getattr(Getitem(possiblepartials, i), "templcsymname"));
	}
      }
    }

    if (posslen > 1) {
      /* Now go through all the possibly matched partial specialization templates and look for a non-ambiguous match.
       * Exact matches rank the highest and deduced parameters are ranked by how specialized they are, eg looking for
       * a match to const int *, the following rank (highest to lowest):
       *   const int * (exact match)
       *   const T *
       *   T *
       *   T
       *
       *   An ambiguous example when attempting to match as either specialization could match: %template() X<int *, double *>;
       *   template<typename T1, typename T2> X class {};  // primary template
       *   template<typename T1> X<T1, double *> class {}; // specialization (1)
       *   template<typename T2> X<int *, T2> class {};    // specialization (2)
       */
      if (template_debug) {
	int row, col;
	int parms_len = ParmList_len(parms);
	Printf(stdout, "      parameter priorities matrix (%d parms):\n", parms_len);
	for (row = 0; row < posslen; row++) {
	  int *priorities_row = priorities_matrix + row*parms_len;
	  Printf(stdout, "        ");
	  for (col = 0; col < parms_len; col++) {
	    Printf(stdout, "%5d ", priorities_row[col]);
	  }
	  Printf(stdout, "\n");
	}
      }
      {
	int row, col;
	int parms_len = ParmList_len(parms);
	/* Printf(stdout, "      parameter priorities inverse matrix (%d parms):\n", parms_len); */
	for (col = 0; col < parms_len; col++) {
	  int *priorities_col = priorities_matrix + col;
	  int maxpriority = -1;
	  /* 
	     Printf(stdout, "max_possible_partials: %d col:%d\n", max_possible_partials, col);
	     Printf(stdout, "        ");
	     */
	  /* determine the highest rank for this nth parameter */
	  for (row = 0; row < posslen; row++) {
	    int *element_ptr = priorities_col + row*parms_len;
	    int priority = *element_ptr;
	    if (priority > maxpriority)
	      maxpriority = priority;
	    /* Printf(stdout, "%5d ", priority); */
	  }
	  /* Printf(stdout, "\n"); */
	  /* flag all the parameters which equal the highest rank */
	  for (row = 0; row < posslen; row++) {
	    int *element_ptr = priorities_col + row*parms_len;
	    int priority = *element_ptr;
	    *element_ptr = (priority >= maxpriority) ? 1 : 0;
	  }
	}
      }
      {
	int row, col;
	int parms_len = ParmList_len(parms);
	Iterator pi = First(possiblepartials);
	Node *chosenpartials = NewList();
	if (template_debug)
	  Printf(stdout, "      priority flags matrix:\n");
	for (row = 0; row < posslen; row++) {
	  int *priorities_row = priorities_matrix + row*parms_len;
	  int highest_count = 0; /* count of highest priority parameters */
	  for (col = 0; col < parms_len; col++) {
	    highest_count += priorities_row[col];
	  }
	  if (template_debug) {
	    Printf(stdout, "        ");
	    for (col = 0; col < parms_len; col++) {
	      Printf(stdout, "%5d ", priorities_row[col]);
	    }
	    Printf(stdout, "\n");
	  }
	  if (highest_count == parms_len) {
	    Append(chosenpartials, pi.item);
	  }
	  pi = Next(pi);
	}
	if (Len(chosenpartials) > 0) {
	  /* one or more best match found */
	  Delete(possiblepartials);
	  possiblepartials = chosenpartials;
	  posslen = Len(possiblepartials);
	} else {
	  /* no best match found */
	  Delete(chosenpartials);
	}
      }
    }

    if (posslen > 0) {
      String *s = Getattr(Getitem(possiblepartials, 0), "templcsymname");
      n = Swig_symbol_clookup_local(s, primary_scope);
      if (posslen > 1) {
	int i;
	if (n) {
	  Swig_warning(WARN_PARSE_TEMPLATE_AMBIG, cparse_file, cparse_line, "Instantiation of template '%s' is ambiguous,\n", SwigType_namestr(tname));
	  Swig_warning(WARN_PARSE_TEMPLATE_AMBIG, Getfile(n), Getline(n), "  instantiation '%s' used,\n", SwigType_namestr(Getattr(n, "name")));
	}
	for (i = 1; i < posslen; i++) {
	  String *templcsymname = Getattr(Getitem(possiblepartials, i), "templcsymname");
	  Node *ignored_node = Swig_symbol_clookup_local(templcsymname, primary_scope);
	  assert(ignored_node);
	  Swig_warning(WARN_PARSE_TEMPLATE_AMBIG, Getfile(ignored_node), Getline(ignored_node), "  instantiation '%s' ignored.\n", SwigType_namestr(Getattr(ignored_node, "name")));
	}
      }
    }

    if (!n) {
      if (template_debug) {
	Printf(stdout, "    chosen primary template: '%s'\n", Getattr(templ, "name"));
      }
      n = templ;
    }
  } else {
    if (template_debug) {
      Printf(stdout, "    primary template not found\n");
    }
    /* Give up if primary (unspecialized) template not found as specializations will only exist if there is a primary template */
    n = 0;
  }

  if (!n) {
    Swig_error(cparse_file, cparse_line, "Template '%s' undefined.\n", name);
  } else if (n) {
    String *nodeType = nodeType(n);
    if (!Equal(nodeType, "template")) {
      Swig_error(cparse_file, cparse_line, "'%s' is not defined as a template. (%s)\n", name, nodeType);
      n = 0;
    }
  }
success:
  Delete(tname);
  Delete(possiblepartials);
  if ((template_debug) && (n)) {
    /*
    Printf(stdout, "Node: %p\n", n);
    Swig_print_node(n);
    */
    Printf(stdout, "    chosen template:'%s'\n", Getattr(n, "name"));
  }
  Delete(parms);
  Free(priorities_matrix);
  return n;
}


/* -----------------------------------------------------------------------------
 * Swig_cparse_template_locate()
 *
 * Search for a template that matches name with given parameters and mark it for instantiation.
 * For templated classes marks the specialized template should there be one.
 * For templated functions marks all the unspecialized templates even if specialized
 * templates exists.
 * ----------------------------------------------------------------------------- */

Node *Swig_cparse_template_locate(String *name, Parm *instantiated_parms, String *symname, Symtab *tscope) {
  Node *match = 0;
  Node *n = template_locate(name, instantiated_parms, symname, tscope); /* this function does what we want for templated classes */

  if (n) {
    String *nodeType = nodeType(n);
    int isclass = 0;
    assert(Equal(nodeType, "template"));
    (void)nodeType;
    isclass = (Equal(Getattr(n, "templatetype"), "class"));

    if (isclass) {
      Parm *tparmsfound = Getattr(n, "templateparms");
      int specialized = !tparmsfound; /* fully specialized (an explicit specialization) */
      int variadic = ParmList_variadic_parm(tparmsfound) != 0;
      if (!specialized) {
	if (!variadic && (ParmList_len(instantiated_parms) > ParmList_len(tparmsfound))) {
	  Swig_error(cparse_file, cparse_line, "Too many template parameters. Maximum of %d.\n", ParmList_len(tparmsfound));
	} else if (ParmList_len(instantiated_parms) < ParmList_numrequired(tparmsfound) - (variadic ? 1 : 0)) { /* Variadic parameter is optional */
	  Swig_error(cparse_file, cparse_line, "Not enough template parameters specified. %d required.\n", (ParmList_numrequired(tparmsfound) - (variadic ? 1 : 0)) );
	}
      }
      SetFlag(n, "instantiate");
      match = n;
    } else {
      Node *firstn = 0;
      /* If not a templated class we must have a templated function.
         The template found is not necessarily the one we want when dealing with templated
         functions. We don't want any specialized templated functions as they won't have
         the default parameters. Let's look for the unspecialized template. Also make sure
         the number of template parameters is correct as it is possible to overload a
         templated function with different numbers of template parameters. */

      if (template_debug) {
	Printf(stdout, "    Not a templated class, seeking all appropriate primary templated functions\n");
      }

      firstn = Swig_symbol_clookup_local(name, 0);
      n = firstn;
      /* First look for all overloaded functions (non-variadic) template matches.
       * Looking for all template parameter matches only (not function parameter matches) 
       * as %template instantiation uses template parameters without any function parameters. */
      while (n) {
	if (Strcmp(nodeType(n), "template") == 0) {
	  Parm *tparmsfound = Getattr(n, "templateparms");
	  if (!ParmList_variadic_parm(tparmsfound)) {
	    if (ParmList_len(instantiated_parms) == ParmList_len(tparmsfound)) {
	      /* successful match */
	      if (template_debug) {
		Printf(stdout, "    found: template <%s> '%s' (%s)\n", ParmList_str_defaultargs(Getattr(n, "templateparms")), name, ParmList_str_defaultargs(Getattr(n, "parms")));
	      }
	      SetFlag(n, "instantiate");
	      if (!match)
		match = n; /* first match */
	    }
	  }
	}
	/* repeat to find all matches with correct number of templated parameters */
	n = Getattr(n, "sym:nextSibling");
      }

      /* Only consider variadic templates if there are no non-variadic template matches */
      if (!match) {
	n = firstn;
	while (n) {
	  if (Strcmp(nodeType(n), "template") == 0) {
	    Parm *tparmsfound = Getattr(n, "templateparms");
	    if (ParmList_variadic_parm(tparmsfound)) {
	      if (ParmList_len(instantiated_parms) >= ParmList_len(tparmsfound) - 1) {
		/* successful variadic match */
		if (template_debug) {
		  Printf(stdout, "    found: template <%s> '%s' (%s)\n", ParmList_str_defaultargs(Getattr(n, "templateparms")), name, ParmList_str_defaultargs(Getattr(n, "parms")));
		}
		SetFlag(n, "instantiate");
		if (!match)
		  match = n; /* first match */
	      }
	    }
	  }
	  /* repeat to find all matches with correct number of templated parameters */
	  n = Getattr(n, "sym:nextSibling");
	}
      }

      if (!match) {
	Swig_error(cparse_file, cparse_line, "Template '%s' undefined.\n", name);
      }
    }
  }

  return match;
}

/* -----------------------------------------------------------------------------
 * merge_parameters()
 *
 * expanded_templateparms are the template parameters passed to %template.
 * This function adds missing parameter name and type attributes from the chosen
 * template (templateparms).
 *
 * Grab the parameter names from templateparms.
 * Non-type template parameters have no type information in expanded_templateparms.
 * Grab them from templateparms.
 *
 * Return 1 if there are variadic template parameters, 0 otherwise.
 * ----------------------------------------------------------------------------- */

static int merge_parameters(ParmList *expanded_templateparms, ParmList *templateparms) {
  Parm *p = expanded_templateparms;
  Parm *tp = templateparms;
  while (p && tp) {
    Setattr(p, "name", Getattr(tp, "name"));
    if (!Getattr(p, "type"))
      Setattr(p, "type", Getattr(tp, "type"));
    p = nextSibling(p);
    tp = nextSibling(tp);
  }
  return ParmList_variadic_parm(templateparms) ? 1 : 0;
}

/* -----------------------------------------------------------------------------
 * mark_defaults()
 *
 * Mark all the template parameters that are expanded from a default value
 * ----------------------------------------------------------------------------- */

static void mark_defaults(ParmList *defaults) {
  Parm *tp = defaults;
  while (tp) {
    Setattr(tp, "default", "1");
    tp = nextSibling(tp);
  }
}

/* -----------------------------------------------------------------------------
 * expand_defaults()
 *
 * Replace parameter types in default argument values, example:
 * input:  int K,int T,class C=Less<(K)>
 * output: int K,int T,class C=Less<(int)>
 * ----------------------------------------------------------------------------- */

static void expand_defaults(ParmList *expanded_templateparms) {
  Parm *tp = expanded_templateparms;
  while (tp) {
    Parm *p = expanded_templateparms;
    String *tv = Getattr(tp, "value");
    if (!tv)
      tv = Getattr(tp, "type");
    while(p) {
      String *name = Getattr(p, "name");
      String *value = Getattr(p, "value");
      if (!value)
	value = Getattr(p, "type");
      if (name)
	Replaceid(tv, name, value);
      p = nextSibling(p);
    }
    tp = nextSibling(tp);
  }
}

/* -----------------------------------------------------------------------------
 * Swig_cparse_template_parms_expand()
 *
 * instantiated_parms: template parameters passed to %template
 * primary: primary template node
 *
 * Expand the instantiated_parms and return a parameter list with default
 * arguments filled in where necessary.
 * ----------------------------------------------------------------------------- */

ParmList *Swig_cparse_template_parms_expand(ParmList *instantiated_parms, Node *primary) {
  ParmList *expanded_templateparms = 0;
  ParmList *templateparms = Getattr(primary, "templateparms");

  if (Equal(Getattr(primary, "templatetype"), "class")) {
    /* Templated class */
    expanded_templateparms = CopyParmList(instantiated_parms);
    int variadic = merge_parameters(expanded_templateparms, templateparms);
    /* Add default arguments from primary template */
    if (!variadic) {
      ParmList *defaults_start = ParmList_nth_parm(templateparms, ParmList_len(instantiated_parms));
      if (defaults_start) {
	ParmList *defaults = CopyParmList(defaults_start);
	mark_defaults(defaults);
	expanded_templateparms = ParmList_join(expanded_templateparms, defaults);
	expand_defaults(expanded_templateparms);
      }
    }
  } else {
    /* Templated function */
    /* TODO: Default template parameters support was only added in C++11 */
    expanded_templateparms = CopyParmList(instantiated_parms);
    merge_parameters(expanded_templateparms, templateparms);
  }

  return expanded_templateparms;
}
