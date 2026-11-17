#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "rblc_internal.h"

#include <errno.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#ifndef RBL_VERSION
#define RBL_VERSION "0.8.1"
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
static int promote_staged_asm(FILE *staged,const char *outpath){
    if(fflush(staged)!=0){perror("rblc-asm: flush staged assembly");return 1;}
    if(fseek(staged,0,SEEK_SET)!=0){perror("rblc-asm: rewind staged assembly");return 1;}
    size_t path_len=strlen(outpath);
    if(path_len>SIZE_MAX-64){fprintf(stderr,"rblc-asm: output path is too long\n");return 1;}
    char*temporary=xmalloc(path_len+64);
    FILE*out=NULL;
    unsigned long stamp=(unsigned long)time(NULL);
    for(unsigned int attempt=0;attempt<128;attempt++){
        snprintf(temporary,path_len+64,"%s.rbltmp.%lu.%u",outpath,stamp,attempt);
        out=fopen(temporary,"wbx");
        if(out)break;
        if(errno!=EEXIST){perror(temporary);free(temporary);return 1;}
    }
    if(!out){fprintf(stderr,"rblc-asm: cannot allocate a unique temporary output beside '%s'\n",outpath);free(temporary);return 1;}
    char buffer[16384];
    int failed=0;
    for(;;){
        size_t count=fread(buffer,1,sizeof(buffer),staged);
        if(count && fwrite(buffer,1,count,out)!=count){perror(temporary);failed=1;break;}
        if(count<sizeof(buffer)){
            if(ferror(staged)){perror("rblc-asm: read staged assembly");failed=1;}
            break;
        }
    }
    if(fflush(out)!=0){perror(temporary);failed=1;}
    if(fclose(out)!=0){perror(temporary);failed=1;}
    if(fclose(staged)!=0){perror("rblc-asm: close staged assembly");failed=1;}
    if(failed){remove(temporary);free(temporary);return 1;}
#ifdef _WIN32
    DWORD attributes=GetFileAttributesA(outpath);
    BOOL committed;
    if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)){
        fprintf(stderr,"rblc-asm: refusing to replace a reparse-point output path: %s\n",outpath);
        remove(temporary);free(temporary);return 1;
    }
    if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY)){
        fprintf(stderr,"rblc-asm: output path is not a regular file: %s\n",outpath);
        remove(temporary);free(temporary);return 1;
    }
    if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)){
        committed=ReplaceFileA(outpath,temporary,NULL,0,NULL,NULL);
    }else{
        committed=MoveFileExA(temporary,outpath,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
    if(!committed){fprintf(stderr,"rblc-asm: cannot atomically replace '%s' (Windows error %lu)\n",outpath,(unsigned long)GetLastError());remove(temporary);free(temporary);return 1;}
#else
    struct stat existing_info;
    if(lstat(outpath,&existing_info)==0){
        if(S_ISLNK(existing_info.st_mode)){
            fprintf(stderr,"rblc-asm: refusing to replace a symbolic-link output path: %s\n",outpath);
            remove(temporary);free(temporary);return 1;
        }
        if(!S_ISREG(existing_info.st_mode)){
            fprintf(stderr,"rblc-asm: output path is not a regular file: %s\n",outpath);
            remove(temporary);free(temporary);return 1;
        }
        /* Preserve the ordinary permission/special mode bits. The atomically
           replaced inode otherwise takes ownership, ACLs, xattrs and timestamps
           from the build process/temp file rather than the previous inode. */
        if(chmod(temporary,existing_info.st_mode&07777)!=0){
            perror(temporary);remove(temporary);free(temporary);return 1;
        }
    }else if(errno!=ENOENT){perror(outpath);remove(temporary);free(temporary);return 1;}
    if(rename(temporary,outpath)!=0){perror(outpath);remove(temporary);free(temporary);return 1;}
#endif
    free(temporary);
    return 0;
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
    if(check){FILE*sink=tmpfile();if(!sink){fprintf(stderr,"rblc-asm: cannot create a temporary file for --check\n");return 1;}emit_program(sink,&prog,input);int failed=0;if(fflush(sink)!=0){perror("rblc-asm: flush --check sink");failed=1;}if(fclose(sink)!=0){perror("rblc-asm: close --check sink");failed=1;}if(failed)return 1;printf("OK: %s\n",input);program_index_free(&prog);free_tokens(toks,nt);free(src);return 0;}
    char buf[4096];if(!outpath){snprintf(buf,sizeof(buf),"%s.s",input);outpath=buf;}
    /* Codegen diagnostics call fatal_at(), which exits the process. Stage the
       assembly first so a failed codegen cannot truncate an existing -S file
       or leave a partial assembly artifact behind. */
    FILE*staged=tmpfile();
    if(!staged){perror("rblc-asm: cannot create staged assembly");return 1;}
    emit_program(staged,&prog,input);
    int output_failed=promote_staged_asm(staged,outpath);
    if(output_failed){program_index_free(&prog);free_tokens(toks,nt);free(src);return 1;}
    fprintf(stderr,"ASM emitted: %s\n",outpath);program_index_free(&prog);free_tokens(toks,nt);free(src);return 0;
}
