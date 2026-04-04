/*
 * IMS SSA (Segment Search Argument) Parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ims.h"

/*
 * SSA Format:
 * 
 * Unqualified: SEGNAME  (8 chars + 1 blank = 9 chars)
 * Qualified:   SEGNAME (FIELD   =value)
 *              SEGNAME (FIELD   >value)
 *              SEGNAME (FIELD  >=value)
 *              SEGNAME (FIELD   <value)
 *              SEGNAME (FIELD  <=value)
 *              SEGNAME (FIELD  !=value)
 * 
 * Multiple qualifications:
 *              SEGNAME (FIELD1 =value1*AND FIELD2 =value2)
 */

/* Parse operator from string */
static SSA_OPERATOR parse_operator(const char *op_str) {
    if (strncmp(op_str, ">=", 2) == 0) return SSA_OP_GE;
    if (strncmp(op_str, "<=", 2) == 0) return SSA_OP_LE;
    if (strncmp(op_str, "!=", 2) == 0) return SSA_OP_NE;
    if (op_str[0] == '=') return SSA_OP_EQ;
    if (op_str[0] == '>') return SSA_OP_GT;
    if (op_str[0] == '<') return SSA_OP_LT;
    return SSA_OP_EQ;  /* Default */
}

/* Parse SSA string into structure */
int ssa_parse(const char *ssa_string, IMS_SSA *ssa) {
    if (!ssa_string || !ssa) {
        return -1;
    }
    
    memset(ssa, 0, sizeof(IMS_SSA));
    
    /* Find segment name (first 8 characters) */
    const char *p = ssa_string;
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && *p != '(' && i < IMS_MAX_SEGMENT_NAME) {
        ssa->segment_name[i++] = toupper((unsigned char)*p);
        p++;
    }
    ssa->segment_name[i] = '\0';
    
    /* Skip whitespace */
    while (*p && isspace((unsigned char)*p)) p++;
    
    /* Check for qualification */
    if (*p == '(') {
        ssa->is_qualified = true;
        p++;  /* Skip '(' */
        
        while (*p && *p != ')') {
            /* Skip whitespace */
            while (*p && isspace((unsigned char)*p)) p++;
            
            /* Check for boolean connector */
            if (strncmp(p, "*AND", 4) == 0) {
                if (ssa->qualification_count > 0) {
                    ssa->qualifications[ssa->qualification_count - 1].bool_op = SSA_BOOL_AND;
                }
                p += 4;
                continue;
            }
            if (strncmp(p, "*OR", 3) == 0) {
                if (ssa->qualification_count > 0) {
                    ssa->qualifications[ssa->qualification_count - 1].bool_op = SSA_BOOL_OR;
                }
                p += 3;
                continue;
            }
            
            /* Parse field name */
            SSA_QUALIFICATION *qual = &ssa->qualifications[ssa->qualification_count];
            i = 0;
            while (*p && !isspace((unsigned char)*p) && 
                   *p != '=' && *p != '>' && *p != '<' && *p != '!' && 
                   i < IMS_MAX_FIELD_NAME) {
                qual->field_name[i++] = toupper((unsigned char)*p);
                p++;
            }
            qual->field_name[i] = '\0';
            
            /* Skip whitespace */
            while (*p && isspace((unsigned char)*p)) p++;
            
            /* Parse operator */
            char op_str[3] = {0};
            if (*p == '>' || *p == '<' || *p == '!' || *p == '=') {
                op_str[0] = *p++;
                if (*p == '=') {
                    op_str[1] = *p++;
                }
            }
            qual->op = parse_operator(op_str);
            
            /* Parse value */
            i = 0;
            while (*p && *p != ')' && *p != '*' && i < 255) {
                qual->value[i++] = *p++;
            }
            qual->value[i] = '\0';
            
            /* Trim trailing spaces from value */
            while (i > 0 && qual->value[i-1] == ' ') {
                qual->value[--i] = '\0';
            }
            
            ssa->qualification_count++;
        }
    }
    
    ims_log("DEBUG", "SSA parsed: segment='%s', qualified=%s, quals=%d",
            ssa->segment_name, 
            ssa->is_qualified ? "YES" : "NO",
            ssa->qualification_count);
    
    return 0;
}

/* Check if segment matches SSA qualifications */
bool ssa_match(IMS_SSA *ssa, IMS_SEGMENT *segment) {
    if (!ssa || !segment) {
        return false;
    }
    
    /* Check segment name first */
    if (strcmp(ssa->segment_name, segment->definition->name) != 0) {
        return false;
    }
    
    /* If unqualified, just matching name is enough */
    if (!ssa->is_qualified) {
        return true;
    }
    
    /* Evaluate qualifications */
    bool result = true;
    SSA_BOOLEAN last_bool = SSA_BOOL_NONE;
    
    for (int q = 0; q < ssa->qualification_count; q++) {
        SSA_QUALIFICATION *qual = &ssa->qualifications[q];
        bool qual_result = false;
        
        /* Find field in segment definition */
        IMS_SEGMENT_DEF *def = segment->definition;
        for (int f = 0; f < def->field_count; f++) {
            if (strcmp(def->fields[f].name, qual->field_name) == 0) {
                /* Get field value from segment data */
                char field_value[256] = {0};
                int len = def->fields[f].length;
                if (len > 255) len = 255;
                memcpy(field_value, segment->data + def->fields[f].offset, len);
                
                /* Trim trailing spaces */
                while (len > 0 && field_value[len-1] == ' ') {
                    field_value[--len] = '\0';
                }
                
                /* Compare based on operator */
                int cmp = strcmp(field_value, qual->value);
                
                switch (qual->op) {
                    case SSA_OP_EQ: qual_result = (cmp == 0); break;
                    case SSA_OP_NE: qual_result = (cmp != 0); break;
                    case SSA_OP_GT: qual_result = (cmp > 0); break;
                    case SSA_OP_GE: qual_result = (cmp >= 0); break;
                    case SSA_OP_LT: qual_result = (cmp < 0); break;
                    case SSA_OP_LE: qual_result = (cmp <= 0); break;
                }
                
                break;
            }
        }
        
        /* Apply boolean logic */
        if (q == 0) {
            result = qual_result;
        } else {
            if (last_bool == SSA_BOOL_AND) {
                result = result && qual_result;
            } else if (last_bool == SSA_BOOL_OR) {
                result = result || qual_result;
            }
        }
        
        last_bool = qual->bool_op;
    }
    
    return result;
}

/* Create SSA programmatically */
IMS_SSA *ssa_create(const char *segment_name) {
    IMS_SSA *ssa = (IMS_SSA *)calloc(1, sizeof(IMS_SSA));
    if (ssa) {
        strncpy(ssa->segment_name, segment_name, IMS_MAX_SEGMENT_NAME);
    }
    return ssa;
}

/* Add qualification to SSA */
int ssa_add_qual(IMS_SSA *ssa, const char *field, SSA_OPERATOR op, 
                 const char *value, SSA_BOOLEAN bool_op) {
    if (!ssa || ssa->qualification_count >= 8) {
        return -1;
    }
    
    SSA_QUALIFICATION *qual = &ssa->qualifications[ssa->qualification_count];
    strncpy(qual->field_name, field, IMS_MAX_FIELD_NAME);
    qual->op = op;
    strncpy(qual->value, value, 255);
    qual->bool_op = bool_op;
    
    ssa->is_qualified = true;
    ssa->qualification_count++;
    
    return 0;
}

/* Display SSA */
void ssa_display(IMS_SSA *ssa) {
    if (!ssa) return;
    
    printf("SSA: %s", ssa->segment_name);
    
    if (ssa->is_qualified) {
        printf(" (");
        for (int i = 0; i < ssa->qualification_count; i++) {
            SSA_QUALIFICATION *q = &ssa->qualifications[i];
            
            const char *op_str = "=";
            switch (q->op) {
                case SSA_OP_EQ: op_str = "="; break;
                case SSA_OP_NE: op_str = "!="; break;
                case SSA_OP_GT: op_str = ">"; break;
                case SSA_OP_GE: op_str = ">="; break;
                case SSA_OP_LT: op_str = "<"; break;
                case SSA_OP_LE: op_str = "<="; break;
            }
            
            printf("%s%s%s", q->field_name, op_str, q->value);
            
            if (q->bool_op == SSA_BOOL_AND) printf(" *AND ");
            else if (q->bool_op == SSA_BOOL_OR) printf(" *OR ");
        }
        printf(")");
    }
    printf("\n");
}
