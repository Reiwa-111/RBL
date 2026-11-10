#include "rblc_internal.h"

#ifndef RBL_VERSION
#define RBL_VERSION "0.8.0"
#endif

static char *read_file_asm(const char*path,size_t*out_len){FILE*f=fopen(path,"rb");if(!f){perror(path);exit(1);}fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<0)fatal("cannot stat input");char*s=xmalloc((size_t)n+1);if(fread(s,1,(size_t)n,f)!=(size_t)n){perror("read");exit(1);}s[n]=0;fclose(f);*out_len=(size_t)n;return s;}
static void validate_source_bytes(const char *file,const char *src,size_t length){
    SourcePos origin={file,0,1,1};
    SourcePos pos=origin;
    for(size_t i=0;i<length;){
        if(src[i]=='\0') fatal_at(pos,"NUL byte is not allowed in a source file");
        size_t width=utf8_sequence_length((const unsigned char *)src,length,i);
        if(!width) fatal_at(pos,"source file is not valid UTF-8");
        if(src[i]=='\r' && i+1<length && src[i+1]=='\n') width=2;
        pos=source_pos_advance(pos,src+i,width);
        i+=width;
    }
}
static void usage_asm(const char*a){fprintf(stderr,"rblc-asm %s\nUsage: %s input.rbl [-S output.s] [--check] [--target linux|win]\n",RBL_VERSION,a);}
int main(int argc,char**argv){
    if(argc<2){usage_asm(argv[0]);return 2;}const char*input=NULL,*outpath=NULL;bool check=false;
    for(int i=1;i<argc;i++){if((!strcmp(argv[i],"-S")||!strcmp(argv[i],"-o"))&&i+1<argc){outpath=argv[++i];continue;}if(!strcmp(argv[i],"--check")){check=true;continue;}if(!strcmp(argv[i],"--target")&&i+1<argc){const char*t=argv[++i];if(!strcmp(t,"win")||!strcmp(t,"win-x86_64")||!strcmp(t,"windows")||!strcmp(t,"windows-x86_64"))g_target=TARGET_WINDOWS_PE;else if(!strcmp(t,"linux")||!strcmp(t,"linux-x86_64")||!strcmp(t,"elf"))g_target=TARGET_LINUX_ELF;else fatal("unknown target '%s' (expected linux or win)",t);continue;}if(!strcmp(argv[i],"--version")){printf("rblc-asm %s\n",RBL_VERSION);return 0;}if(argv[i][0]=='-'){usage_asm(argv[0]);return 2;}if(!input)input=argv[i];else{usage_asm(argv[0]);return 2;}}
    if(!input){usage_asm(argv[0]);return 2;}size_t src_len=0;char*src=read_file_asm(input,&src_len);diagnostic_set_source(input,src,src_len);
    diagnostic_set_stage(DIAG_STAGE_LEXER);validate_source_bytes(input,src,src_len);
    size_t nt=0;diagnostic_set_stage(DIAG_STAGE_LEXER);Token*toks=lex_all(src,input,&nt);
    diagnostic_set_stage(DIAG_STAGE_PARSER);Program prog=parse_program(toks,nt,input);
    diagnostic_set_stage(DIAG_STAGE_SEMANTIC);validate_program(&prog,toks[nt-1].pos);
    /* Some diagnostics are produced by code generation (break/continue outside a
       loop, for example), so --check must run the back end too instead of stopping
       after parsing; the assembly itself goes to a throwaway stream. */
    diagnostic_set_stage(DIAG_STAGE_CODEGEN);
    if(check){FILE*sink=tmpfile();if(!sink){fprintf(stderr,"rblc-asm: cannot create a temporary file for --check\n");return 1;}emit_program(sink,&prog,input);fclose(sink);printf("OK: %s\n",input);program_index_free(&prog);free_tokens(toks,nt);free(src);return 0;}
    char buf[4096];if(!outpath){snprintf(buf,sizeof(buf),"%s.s",input);outpath=buf;}FILE*out=fopen(outpath,"wb");if(!out){perror(outpath);return 1;}emit_program(out,&prog,input);fclose(out);fprintf(stderr,"ASM emitted: %s\n",outpath);program_index_free(&prog);free_tokens(toks,nt);free(src);return 0;
}
