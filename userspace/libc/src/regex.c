#include <regex.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* AST Node types for regex */
enum node_type {
    NODE_CHAR,
    NODE_ANY,           /* . */
    NODE_CLASS,         /* [...] */
    NODE_BOL,           /* ^ */
    NODE_EOL,           /* $ */
    NODE_STAR,          /* * */
    NODE_PLUS,          /* + */
    NODE_OPT,           /* ? */
    NODE_CAT,           /* concatenation */
    NODE_ALT,           /* | */
    NODE_GROUP          /* ( ... ) */
};

struct char_class {
    unsigned char bits[32]; /* 256 bits */
    int negated;
};

struct regex_node {
    enum node_type type;
    int ch;
    struct char_class cls;
    int group_index;
    struct regex_node *left;
    struct regex_node *right;
};

struct compiled_regex {
    struct regex_node *root;
    int cflags;
    size_t nsub;
};

static void free_node(struct regex_node *node) {
    if (!node) return;
    free_node(node->left);
    free_node(node->right);
    free(node);
}

static struct regex_node *new_node(enum node_type type) {
    struct regex_node *n = (struct regex_node *)calloc(1, sizeof(struct regex_node));
    if (n) n->type = type;
    return n;
}

/* Parser state */
struct parse_state {
    const char *p;
    int cflags;
    int group_count;
    int error;
};

static struct regex_node *parse_expr(struct parse_state *s);
static struct regex_node *parse_branch(struct parse_state *s);
static struct regex_node *parse_piece(struct parse_state *s);
static struct regex_node *parse_atom(struct parse_state *s);

static struct regex_node *parse_atom(struct parse_state *s) {
    if (!*s->p) return NULL;

    if (*s->p == '^') {
        s->p++;
        return new_node(NODE_BOL);
    }
    if (*s->p == '$') {
        s->p++;
        return new_node(NODE_EOL);
    }
    if (*s->p == '.') {
        s->p++;
        return new_node(NODE_ANY);
    }
    if (*s->p == '(') {
        s->p++;
        int gidx = ++s->group_count;
        struct regex_node *sub = parse_expr(s);
        if (*s->p != ')') {
            s->error = REG_EPAREN;
            free_node(sub);
            return NULL;
        }
        s->p++;
        struct regex_node *grp = new_node(NODE_GROUP);
        if (!grp) { free_node(sub); s->error = REG_ESPACE; return NULL; }
        grp->group_index = gidx;
        grp->left = sub;
        return grp;
    }
    if (*s->p == '[') {
        s->p++;
        struct regex_node *n = new_node(NODE_CLASS);
        if (!n) { s->error = REG_ESPACE; return NULL; }
        if (*s->p == '^') {
            n->cls.negated = 1;
            s->p++;
        }
        int first = 1;
        while (*s->p && (*s->p != ']' || first)) {
            first = 0;
            unsigned char c1 = (unsigned char)*s->p++;
            if (*s->p == '-' && s->p[1] && s->p[1] != ']') {
                s->p++;
                unsigned char c2 = (unsigned char)*s->p++;
                if (c1 > c2) { unsigned char t = c1; c1 = c2; c2 = t; }
                for (int c = c1; c <= c2; c++) {
                    n->cls.bits[c / 8] |= (1 << (c % 8));
                    if (s->cflags & REG_ICASE) {
                        int lc = tolower(c), uc = toupper(c);
                        n->cls.bits[lc / 8] |= (1 << (lc % 8));
                        n->cls.bits[uc / 8] |= (1 << (uc % 8));
                    }
                }
            } else {
                n->cls.bits[c1 / 8] |= (1 << (c1 % 8));
                if (s->cflags & REG_ICASE) {
                    int lc = tolower(c1), uc = toupper(c1);
                    n->cls.bits[lc / 8] |= (1 << (lc % 8));
                    n->cls.bits[uc / 8] |= (1 << (uc % 8));
                }
            }
        }
        if (*s->p == ']') s->p++;
        else { free_node(n); s->error = REG_EBRACK; return NULL; }
        return n;
    }
    if (*s->p == '\\') {
        s->p++;
        if (!*s->p) { s->error = REG_EESCAPE; return NULL; }
        struct regex_node *n = new_node(NODE_CHAR);
        if (!n) { s->error = REG_ESPACE; return NULL; }
        n->ch = (unsigned char)*s->p++;
        return n;
    }

    /* Regular character */
    struct regex_node *n = new_node(NODE_CHAR);
    if (!n) { s->error = REG_ESPACE; return NULL; }
    n->ch = (unsigned char)*s->p++;
    return n;
}

static struct regex_node *parse_piece(struct parse_state *s) {
    struct regex_node *atom = parse_atom(s);
    if (!atom || s->error) return atom;

    if (*s->p == '*' || *s->p == '+' || *s->p == '?') {
        char op = *s->p++;
        struct regex_node *rep = new_node(op == '*' ? NODE_STAR : (op == '+' ? NODE_PLUS : NODE_OPT));
        if (!rep) { free_node(atom); s->error = REG_ESPACE; return NULL; }
        rep->left = atom;
        return rep;
    }
    return atom;
}

static struct regex_node *parse_branch(struct parse_state *s) {
    struct regex_node *chain = NULL;
    while (*s->p && *s->p != '|' && *s->p != ')') {
        struct regex_node *piece = parse_piece(s);
        if (!piece || s->error) {
            free_node(chain);
            return NULL;
        }
        if (!chain) {
            chain = piece;
        } else {
            struct regex_node *cat = new_node(NODE_CAT);
            if (!cat) { free_node(chain); free_node(piece); s->error = REG_ESPACE; return NULL; }
            cat->left = chain;
            cat->right = piece;
            chain = cat;
        }
    }
    return chain ? chain : new_node(NODE_CHAR); /* empty matches empty */
}

static struct regex_node *parse_expr(struct parse_state *s) {
    struct regex_node *branch = parse_branch(s);
    if (!branch || s->error) return NULL;

    while (*s->p == '|') {
        s->p++;
        struct regex_node *next_branch = parse_branch(s);
        if (!next_branch || s->error) {
            free_node(branch);
            return NULL;
        }
        struct regex_node *alt = new_node(NODE_ALT);
        if (!alt) { free_node(branch); free_node(next_branch); s->error = REG_ESPACE; return NULL; }
        alt->left = branch;
        alt->right = next_branch;
        branch = alt;
    }
    return branch;
}

int regcomp(regex_t *preg, const char *regex, int cflags) {
    if (!preg || !regex) return REG_BADPAT;

    struct parse_state s;
    memset(&s, 0, sizeof(s));
    s.p = regex;
    s.cflags = cflags;

    struct regex_node *root = parse_expr(&s);
    if (s.error != 0 || !root) {
        free_node(root);
        return s.error ? s.error : REG_BADPAT;
    }

    struct compiled_regex *cr = (struct compiled_regex *)calloc(1, sizeof(struct compiled_regex));
    if (!cr) {
        free_node(root);
        return REG_ESPACE;
    }
    cr->root = root;
    cr->cflags = cflags;
    cr->nsub = (size_t)s.group_count;

    preg->re_comp = cr;
    preg->re_cflags = cflags;
    preg->re_nsub = cr->nsub;
    return REG_OK;
}

void regfree(regex_t *preg) {
    if (!preg || !preg->re_comp) return;
    struct compiled_regex *cr = (struct compiled_regex *)preg->re_comp;
    free_node(cr->root);
    free(cr);
    preg->re_comp = NULL;
}

/* Matcher execution */
struct match_state {
    const char *orig_str;
    const char *str;
    int cflags;
    int eflags;
    size_t nmatch;
    regmatch_t *pmatch;
};

static int match_node(const struct regex_node *n, const char *cur, const char **next, struct match_state *ms) {
    if (!n) {
        *next = cur;
        return 1;
    }

    switch (n->type) {
    case NODE_CHAR: {
        if (!*cur) return 0;
        int c1 = (unsigned char)*cur;
        int c2 = n->ch;
        if (ms->cflags & REG_ICASE) {
            c1 = tolower(c1);
            c2 = tolower(c2);
        }
        if (c1 != c2) return 0;
        *next = cur + 1;
        return 1;
    }
    case NODE_ANY: {
        if (!*cur) return 0;
        if (*cur == '\n' && (ms->cflags & REG_NEWLINE)) return 0;
        *next = cur + 1;
        return 1;
    }
    case NODE_CLASS: {
        if (!*cur) return 0;
        unsigned char c = (unsigned char)*cur;
        int in_set = (n->cls.bits[c / 8] & (1 << (c % 8))) != 0;
        if (n->cls.negated) in_set = !in_set;
        if (!in_set) return 0;
        *next = cur + 1;
        return 1;
    }
    case NODE_BOL: {
        if (cur == ms->orig_str) {
            if (ms->eflags & REG_NOTBOL) return 0;
            *next = cur;
            return 1;
        }
        if ((ms->cflags & REG_NEWLINE) && cur > ms->orig_str && cur[-1] == '\n') {
            *next = cur;
            return 1;
        }
        return 0;
    }
    case NODE_EOL: {
        if (*cur == '\0') {
            if (ms->eflags & REG_NOTEOL) return 0;
            *next = cur;
            return 1;
        }
        if ((ms->cflags & REG_NEWLINE) && *cur == '\n') {
            *next = cur;
            return 1;
        }
        return 0;
    }
    case NODE_CAT: {
        const char *mid = NULL;
        if (!match_node(n->left, cur, &mid, ms)) return 0;
        return match_node(n->right, mid, next, ms);
    }
    case NODE_ALT: {
        if (match_node(n->left, cur, next, ms)) return 1;
        return match_node(n->right, cur, next, ms);
    }
    case NODE_OPT: {
        if (match_node(n->left, cur, next, ms)) return 1;
        *next = cur;
        return 1;
    }
    case NODE_STAR: {
        /* Greedy matching with fallback */
        const char *p = cur;
        const char *adv = NULL;
        const char *matched_ends[256];
        int count = 0;
        matched_ends[count++] = cur;
        while (count < 255 && match_node(n->left, p, &adv, ms) && adv > p) {
            p = adv;
            matched_ends[count++] = p;
        }
        /* Try longest to shortest */
        for (int i = count - 1; i >= 0; i--) {
            *next = matched_ends[i];
            return 1;
        }
        *next = cur;
        return 1;
    }
    case NODE_PLUS: {
        const char *first_end = NULL;
        if (!match_node(n->left, cur, &first_end, ms)) return 0;
        const char *p = first_end;
        const char *adv = NULL;
        const char *matched_ends[256];
        int count = 0;
        matched_ends[count++] = first_end;
        while (count < 255 && match_node(n->left, p, &adv, ms) && adv > p) {
            p = adv;
            matched_ends[count++] = p;
        }
        for (int i = count - 1; i >= 0; i--) {
            *next = matched_ends[i];
            return 1;
        }
        *next = first_end;
        return 1;
    }
    case NODE_GROUP: {
        regoff_t start_off = (regoff_t)(cur - ms->orig_str);
        const char *adv = NULL;
        if (!match_node(n->left, cur, &adv, ms)) return 0;
        if (ms->pmatch && (size_t)n->group_index < ms->nmatch) {
            ms->pmatch[n->group_index].rm_so = start_off;
            ms->pmatch[n->group_index].rm_eo = (regoff_t)(adv - ms->orig_str);
        }
        *next = adv;
        return 1;
    }
    default:
        return 0;
    }
}

int regexec(const regex_t *preg, const char *string, size_t nmatch,
            regmatch_t pmatch[], int eflags) {
    if (!preg || !preg->re_comp || !string) return REG_NOMATCH;

    struct compiled_regex *cr = (struct compiled_regex *)preg->re_comp;
    struct match_state ms;
    ms.orig_str = string;
    ms.cflags = cr->cflags;
    ms.eflags = eflags;
    ms.nmatch = nmatch;
    ms.pmatch = pmatch;

    for (size_t i = 0; i < nmatch; i++) {
        pmatch[i].rm_so = -1;
        pmatch[i].rm_eo = -1;
    }

    const char *start = string;
    while (*start) {
        const char *end = NULL;
        if (match_node(cr->root, start, &end, &ms)) {
            if (nmatch > 0 && pmatch) {
                pmatch[0].rm_so = (regoff_t)(start - string);
                pmatch[0].rm_eo = (regoff_t)(end - string);
            }
            return REG_OK;
        }
        if (cr->root->type == NODE_BOL) break; /* bol only matches at start */
        start++;
    }

    /* Try matching empty string at end of string */
    const char *end = NULL;
    if (match_node(cr->root, start, &end, &ms)) {
        if (nmatch > 0 && pmatch) {
            pmatch[0].rm_so = (regoff_t)(start - string);
            pmatch[0].rm_eo = (regoff_t)(end - string);
        }
        return REG_OK;
    }

    return REG_NOMATCH;
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size) {
    (void)preg;
    const char *msg = "Success";
    switch (errcode) {
    case REG_NOMATCH: msg = "No match"; break;
    case REG_BADPAT:  msg = "Invalid regular expression"; break;
    case REG_EPAREN:  msg = "Unmatched parentheses"; break;
    case REG_EBRACK:  msg = "Unmatched brackets"; break;
    case REG_EESCAPE: msg = "Trailing backslash"; break;
    case REG_ESPACE:  msg = "Out of memory"; break;
    default: msg = "Unknown regex error"; break;
    }
    size_t len = strlen(msg) + 1;
    if (errbuf && errbuf_size > 0) {
        snprintf(errbuf, errbuf_size, "%s", msg);
    }
    return len;
}
