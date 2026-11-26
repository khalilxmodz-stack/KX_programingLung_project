#pragma once
#include <cctype>
#include <string>
#include <vector>
#include <sstream>
#include <utility>

namespace kxparse {
struct Token { std::string text; int line=1; int column=1; enum Kind { Identifier, Number, String, Symbol, End } kind=End; };
struct Node { std::string kind, text; int line=1; int column=1; std::vector<Node> children; };
struct Result { std::vector<Node> declarations; std::vector<Token> tokens; std::string error; };

class Lexer {
    const std::string& src; size_t p=0; int line=1;
    bool starts(const char* s) const { size_t n=0; while(s[n]) ++n; return src.compare(p,n,s)==0; }
    int columnAt(size_t pos) const {
        const size_t lineStart = src.rfind('\n', pos == 0 ? 0 : pos - 1);
        return static_cast<int>(pos - (lineStart == std::string::npos ? 0 : lineStart + 1)) + 1;
    }
public:
    explicit Lexer(const std::string& s):src(s){}
    std::vector<Token> run(std::string& error) {
        std::vector<Token> out;
        const char* ops[]={"...", "->", "++", "--", "+=", "-=", "*=", "/=", "%=", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "&=", "|=", "^="};
        while(p<src.size()) {
            char c=src[p];
            if(c=='\n'){++line;++p;continue;} if(std::isspace((unsigned char)c)){++p;continue;}
            if(starts("//")){while(p<src.size()&&src[p]!='\n')++p;continue;}
            if(starts("/*")){p+=2;bool closed=false;while(p<src.size()){if(src[p]=='\n')++line;if(starts("*/")){p+=2;closed=true;break;}++p;}if(!closed){error="line "+std::to_string(line)+":"+std::to_string(columnAt(p))+": unterminated block comment";return {};}continue;}
            Token t; t.line=line; t.column=columnAt(p);
            if(std::isalpha((unsigned char)c)||c=='_'){t.kind=Token::Identifier;size_t b=p++;while(p<src.size()&&(std::isalnum((unsigned char)src[p])||src[p]=='_'))++p;t.text=src.substr(b,p-b);}
            else if(std::isdigit((unsigned char)c)){t.kind=Token::Number;size_t b=p++;while(p<src.size()&&(std::isalnum((unsigned char)src[p])||src[p]=='.'||src[p]=='_'))++p;t.text=src.substr(b,p-b);}
            else if(c=='"'||c=='\''){t.kind=Token::String;char q=c;size_t b=p++;bool closed=false;while(p<src.size()){if(src[p]=='\\'){p+=p+1<src.size()?2:1;continue;}if(src[p]==q){++p;closed=true;break;}if(src[p]=='\n')++line;++p;}if(!closed){error="line "+std::to_string(t.line)+":"+std::to_string(t.column)+": unterminated string literal";return {};}t.text=src.substr(b,p-b);}
            else {t.kind=Token::Symbol;bool matched=false;for(const char* op:ops){if(starts(op)){t.text=op;size_t n=0;while(op[n])++n;p+=n;matched=true;break;}}if(!matched){t.text.assign(1,c);++p;}}
            out.push_back(std::move(t));
        }
        out.push_back(Token{"<eof>",line,columnAt(p),Token::End}); return out;
    }
};

class Parser {
    const std::vector<Token>& t; size_t p=0; std::string err;
    const Token& cur() const { return t[p<t.size()?p:t.size()-1]; }
    bool is(const char* s) const { return cur().text==s; }
    Token take(){Token x=cur();if(p+1<t.size())++p;return x;}
    bool accept(const char* s){if(is(s)){take();return true;}return false;}
    bool need(const char* s){if(accept(s))return true;fail(std::string("expected '")+s+"', found '"+cur().text+"'");return false;}
    void fail(const std::string& m){if(err.empty())err="line "+std::to_string(cur().line)+":"+std::to_string(cur().column)+": "+m;}
    bool name(){return cur().kind==Token::Identifier;}
    Node node(std::string k,std::string x=""){Node n;n.kind=std::move(k);n.text=std::move(x);n.line=cur().line;n.column=cur().column;return n;}
    static int prec(const std::string& op){if(op==",")return 1;if(op=="="||op=="+="||op=="-="||op=="*="||op=="/="||op=="%="||op=="&="||op=="|="||op=="^=")return 2;if(op=="?")return 3;if(op=="||")return 4;if(op=="&&")return 5;if(op=="|")return 6;if(op=="^")return 7;if(op=="&")return 8;if(op=="=="||op=="!=")return 9;if(op=="<"||op==">"||op=="<="||op==">=")return 10;if(op=="<<"||op==">>")return 11;if(op=="+"||op=="-")return 12;if(op=="*"||op=="/"||op=="%")return 13;return 0;}
    bool rightAssoc(const std::string& op){return prec(op)==2||op=="?";}
    bool typeStart() const {return is("i8")||is("i16")||is("i32")||is("i64")||is("u8")||is("u16")||is("u32")||is("u64")||is("bool")||is("void")||is("ptr")||is("struct")||is("enum")||is("const")||is("volatile");}
    Node parseType(){Node n=node("type");if(accept("const"))n.text="const ";if(accept("volatile"))n.text+="volatile ";if(accept("struct")||accept("enum")){n.text+=t[p-1].text+" ";if(!name()){fail("expected type name");return n;}n.text+=take().text;}else if(name())n.text+=take().text;else{fail("expected type name");return n;}while(accept("*"))n.text+="*";return n;}
    Node expression(int min=1){Node left=prefix();while(err.empty()){
        if(is("(")&&left.kind=="identifier"){take();Node call=node("call",left.text);if(!is(")")){do{call.children.push_back(expression(2));}while(accept(","));}need(")");left=std::move(call);continue;}
        if(accept("[")){Node ix=node("index");ix.children.push_back(std::move(left));ix.children.push_back(expression());need("]");left=std::move(ix);continue;}
        if(is(".")||is("->")){std::string op=take().text;Node m=node("member",op);m.children.push_back(std::move(left));if(!name())fail("expected member name");else m.text+=" "+take().text;left=std::move(m);continue;}
        if(is("++")||is("--")){std::string op=take().text;Node post=node("postfix",op);post.children.push_back(std::move(left));left=std::move(post);continue;}
        std::string op=cur().text;int q=prec(op);if(q<min||q==0)break;take();if(op=="?"){Node c=node("conditional",op);c.children.push_back(std::move(left));c.children.push_back(expression());need(":");c.children.push_back(expression(q));left=std::move(c);continue;}Node b=node("binary",op);b.children.push_back(std::move(left));b.children.push_back(expression(q+(rightAssoc(op)?0:1)));left=std::move(b);
    }return left;}
    Node prefix(){if(is("+")||is("-")||is("!")||is("~")||is("*")||is("&")||is("++")||is("--")){std::string op=take().text;Node n=node("unary",op);n.children.push_back(expression(14));return n;}if(accept("(")){Node n=expression();need(")");return n;}if(cur().kind==Token::Number||cur().kind==Token::String){Token x=take();return node(x.kind==Token::String?"string":"number",x.text);}if(name())return node("identifier",take().text);fail("expected expression");return node("error");}
    Node block(){Node b=node("block");if(!need("{"))return b;while(!is("}")&&!is("<eof>")&&err.empty())b.children.push_back(statement());need("}");return b;}
    Node statement(){if(is("{"))return block();if(accept("if")){Node n=node("if");need("(");n.children.push_back(expression());need(")");n.children.push_back(statement());if(accept("else"))n.children.push_back(statement());return n;}if(accept("while")){Node n=node("while");need("(");n.children.push_back(expression());need(")");n.children.push_back(statement());return n;}if(accept("do")){Node n=node("do_while");n.children.push_back(statement());need("while");need("(");n.children.push_back(expression());need(")");need(";");return n;}if(accept("for")){Node n=node("for");need("(");if(!is(";"))n.children.push_back(declaration(false));else n.children.push_back(node("empty"));need(";");if(!is(";"))n.children.push_back(expression());else n.children.push_back(node("empty"));need(";");if(!is(")"))n.children.push_back(expression());else n.children.push_back(node("empty"));need(")");n.children.push_back(statement());return n;}if(accept("switch")){Node n=node("switch");need("(");n.children.push_back(expression());need(")");need("{");while(!is("}")&&!is("<eof>")&&err.empty()){if(accept("case")){Node c=node("case");c.children.push_back(expression(2));need(":");while(!is("case")&&!is("default")&&!is("}")&&!is("<eof>"))c.children.push_back(statement());n.children.push_back(std::move(c));}else if(accept("default")){Node c=node("default");need(":");while(!is("case")&&!is("}")&&!is("<eof>"))c.children.push_back(statement());n.children.push_back(std::move(c));}else{fail("expected case/default in switch");break;}}need("}");return n;}if(accept("return")){Node n=node("return");if(!is(";"))n.children.push_back(expression());need(";");return n;}if(accept("break")){Node n=node("break");need(";");return n;}if(accept("continue")){Node n=node("continue");need(";");return n;}if(is("kx")&&p+1<t.size()&&t[p+1].text=="{"){take();Node n=node("legacy_instruction_block");need("{");while(!is("}")&&!is("<eof>")&&err.empty()){n.text+=take().text+" ";}need("}");accept(";");return n;}if(is("kx_asm")&&p+1<t.size()&&t[p+1].text=="{"){take();Node n=node("asm_block");need("{");while(!is("}")&&!is("<eof>")&&err.empty()){if(cur().kind==Token::String)n.text+=take().text;else n.text+=take().text+" ";}need("}");accept(";");return n;}if(accept("asm")){Node n=node("asm");if(accept("volatile"))n.text="volatile";need("(");if(cur().kind!=Token::String)fail("asm expects a string literal");else n.text+=" "+take().text;while(accept(",")){if(cur().kind==Token::String)n.text+=" "+take().text;else if(name())n.text+=" "+take().text;else fail("invalid asm operand");}need(")");need(";");return n;}if(typeStart())return declaration(true);Node n=node("expression_statement");n.children.push_back(expression());need(";");return n;}
    Node declaration(bool semi){Node n=node("declaration");Node ty=parseType();n.text=ty.text;do{if(!name()){fail("expected variable name");break;}Node v=node("variable",take().text);while(accept("[")){v.text+="[]";if(!is("]"))v.children.push_back(expression());need("]");}if(accept("=")){if(accept("{")){Node init=node("initializer_list");if(!is("}")){do{init.children.push_back(expression(2));}while(accept(",")&&!is("}"));}need("}");v.children.push_back(std::move(init));}else v.children.push_back(expression(2));}n.children.push_back(std::move(v));}while(accept(","));if(semi)need(";");return n;}
    Node aggregate(const char* kw){take();Node n=node(kw);if(!name()){fail(std::string("expected ")+kw+" name");return n;}n.text=take().text;need("{");while(!is("}")&&!is("<eof>")&&err.empty()){if(std::string(kw)=="struct"){Node f=node("field");f.text=parseType().text;if(!name())fail("expected field name");else f.text+=" "+take().text;while(accept("[")){if(!is("]"))f.children.push_back(expression());need("]");}need(";");n.children.push_back(std::move(f));}else{Node e=node("enumerator");if(!name())fail("expected enumerator");else e.text=take().text;if(accept("="))e.children.push_back(expression(2));n.children.push_back(std::move(e));if(!accept(",")&&!is("}"))fail("expected ',' in enum");}}need("}");accept(";");return n;}
    Node function(){need("fn");Node f=node("function");if(!name())fail("expected function name");else f.text=take().text;need("(");if(!is(")")){do{Node p=node("parameter");p.text=parseType().text;if(!name())fail("expected parameter name");else p.text+=" "+take().text;f.children.push_back(std::move(p));}while(accept(","));}need(")");need("->");Node r=parseType();f.text+=" -> "+r.text;f.children.push_back(block());return f;}
public:
    explicit Parser(const std::vector<Token>& tokens):t(tokens){}
    Result run(){Result r;while(!is("<eof>")&&err.empty()){if(is("struct"))r.declarations.push_back(aggregate("struct"));else if(is("enum"))r.declarations.push_back(aggregate("enum"));else if(is("fn"))r.declarations.push_back(function());else{fail("expected top-level struct, enum, or fn declaration");break;}}r.tokens=t;r.error=err;return r;}
};
inline Result parse(const std::string& src){std::string e;Lexer l(src);auto tokens=l.run(e);if(!e.empty()){Result r;r.error=e;return r;}return Parser(tokens).run();}

// Embed a useful subset of the legacy KX instruction language in a C-like
// function. These are KX virtual instructions (variables/labels), not target
// assembler. They are lowered as control/arithmetic statements for now.
inline std::string rewriteLegacyBlocks(const std::string& src,std::string& error){
    std::string out; size_t i=0;
    while(i<src.size()){
        if(src[i]=='"'||src[i]=='\''){char q=src[i];out+=src[i++];while(i<src.size()){char c=src[i++];out+=c;if(c=='\\'&&i<src.size()){out+=src[i++];continue;}if(c==q)break;}continue;}
        if(i+1<src.size()&&src[i]=='/'&&src[i+1]=='/'){while(i<src.size()){char c=src[i++];out+=c;if(c=='\n')break;}continue;}
        if(i+1<src.size()&&src[i]=='/'&&src[i+1]=='*'){out+="/*";i+=2;while(i<src.size()){char c=src[i++];out+=c;if(c=='*'&&i<src.size()&&src[i]=='/'){out+='/';++i;break;}}continue;}
        if(src.compare(i,2,"kx")==0&&(i==0||!(std::isalnum((unsigned char)src[i-1])||src[i-1]=='_'))){
            size_t j=i+2;while(j<src.size()&&std::isspace((unsigned char)src[j]))++j;
            if(j<src.size()&&src[j]=='{'){
                size_t b=++j;bool quote=false;char q=0;bool esc=false;
                while(j<src.size()){char c=src[j];if(quote){if(esc)esc=false;else if(c=='\\')esc=true;else if(c==q)quote=false;}else if(c=='"'||c=='\''){quote=true;q=c;}else if(c=='}')break;++j;}
                if(j>=src.size()){error="unterminated kx instruction block";return {};}
                std::string body=src.substr(b,j-b),generated="{ int __kx_cmp = 0;\n";
                std::istringstream lines(body);std::string line;
                while(std::getline(lines,line)){size_t comment=line.find("//");if(comment!=std::string::npos)line.resize(comment);size_t semi=0;
                    while(semi<line.size()){
                        size_t end=line.find(';',semi);if(end==std::string::npos)end=line.size();std::string stmt=line.substr(semi,end-semi);semi=end+1;
                        std::istringstream wordsIn(stmt);std::vector<std::string> w;std::string z;while(wordsIn>>z){if(z==",")continue;if(!z.empty()&&z.back()==',')z.pop_back();if(!z.empty())w.push_back(z);}
                        if(w.empty())continue;const std::string& op=w[0];
                        auto need=[&](size_t n){if(w.size()!=n){error="invalid operand count for KX instruction '"+op+"' in embedded block";return false;}return true;};
                        if(op=="mov"){if(!need(3))return {};generated+="  "+w[1]+" = "+w[2]+";\n";}
                        else if(op=="add"||op=="sub"||op=="mul"||op=="sdiv"||op=="srem"||op=="and"||op=="or"||op=="xor"||op=="shl"||op=="ashr"){
                            if(!need(3))return {};std::string oper=op=="add"?"+=":op=="sub"?"-=":op=="mul"?"*=":op=="sdiv"?"/=":op=="srem"?"%=":op=="and"?"&=":op=="or"?"|=":op=="xor"?"^=":op=="shl"?"<<=":">>=";
                            generated+="  "+w[1]+" "+oper+" "+w[2]+";\n";
                        }
                        else if(op=="inc"||op=="dec"){if(!need(2))return {};generated+="  "+w[1]+(op=="inc"?"++;\n":"--;\n");}
                        else if(op=="neg"||op=="not"){if(!need(2))return {};generated+="  "+w[1]+" = "+(op=="neg"?"-":"~")+w[1]+";\n";}
                        else if(op=="cmp"){if(!need(3))return {};generated+="  __kx_cmp = ("+w[1]+" == "+w[2]+") ? 0 : (("+w[1]+" < "+w[2]+") ? -1 : 1);\n";}
                        else if(op=="jmp"){if(!need(2))return {};generated+="  goto "+w[1]+";\n";}
                        else if(op=="je"||op=="jne"){if(!need(2))return {};generated+=std::string("  if (__kx_cmp ")+(op=="je"?"==":"!=")+" 0) goto "+w[1]+";\n";}
                        else if(op=="label"){if(!need(2))return {};generated+=""+w[1]+": ;\n";}
                        else if(op=="print"){if(!need(2))return {};generated+="  print("+w[1]+");\n";}
                        else if(op=="halt"){if(!need(1))return {};generated+="  return 0;\n";}
                        else if(op=="ret"){if(w.size()>2){error="ret accepts zero or one operand";return {};}generated+="  return"+(w.size()==2?" "+w[1]:"")+";\n";}
                        else {error="unsupported embedded KX instruction '"+op+"'";return {};}
                    }
                }
                generated+="}";out+=generated;i=j+1;if(i<src.size()&&src[i]==';')++i;continue;
            }
        }
        out+=src[i++];
    }
    return out;
}

// Converts the explicit KX machine-assembly block into a compiler inline-asm
// statement. This is intentionally distinct from the legacy KX IR instruction
// frontend: contents are target assembler text (for example ARM or x86 syntax).
inline std::string rewriteAsmBlocks(const std::string& src,std::string& error){
    std::string out; size_t i=0;
    while(i<src.size()){
        if(src[i]=='"'||src[i]=='\''){char q=src[i];out+=src[i++];while(i<src.size()){char c=src[i++];out+=c;if(c=='\\'&&i<src.size()){out+=src[i++];continue;}if(c==q)break;}continue;}
        if(i+1<src.size()&&src[i]=='/'&&src[i+1]=='/'){while(i<src.size()){char c=src[i++];out+=c;if(c=='\n')break;}continue;}
        if(i+1<src.size()&&src[i]=='/'&&src[i+1]=='*'){out+="/*";i+=2;while(i<src.size()){char c=src[i++];out+=c;if(c=='*'&&i<src.size()&&src[i]=='/'){out+='/';++i;break;}}continue;}
        if(src.compare(i,6,"kx_asm")==0&&(i==0||!(std::isalnum((unsigned char)src[i-1])||src[i-1]=='_'))){
            size_t j=i+6;while(j<src.size()&&std::isspace((unsigned char)src[j]))++j;
            if(j<src.size()&&src[j]=='{'){
                size_t b=++j;bool quote=false;char q=0;bool esc=false;
                while(j<src.size()) {char c=src[j];if(quote){if(esc)esc=false;else if(c=='\\')esc=true;else if(c==q)quote=false;}else if(c=='"'||c=='\''){quote=true;q=c;}else if(c=='}')break;++j;}
                if(j>=src.size()){error="unterminated kx_asm block";return {};}
                std::string code=src.substr(b,j-b),escaped;
                for(char c:code){if(c=='\\'||c=='"')escaped+='\\';if(c=='\n')escaped+="\\n";else if(c=='\r'){}else escaped+=c;}
                out+="__asm__ volatile(\""+escaped+"\");";i=j+1;if(i<src.size()&&src[i]==';')++i;continue;
            }
        }
        out+=src[i++];
    }
    return out;
}
}
