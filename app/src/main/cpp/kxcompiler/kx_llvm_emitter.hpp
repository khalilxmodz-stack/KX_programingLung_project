#pragma once
#include "kx_typed_ir.hpp"
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <set>
#include <tuple>

namespace kxir {
class Emitter {
 const Module& module; std::ostringstream ir,body,allocas; std::map<std::string,Type> vars; std::map<std::string,std::string> slots;
 int temp=0,label=0; std::string error; bool freestanding=false,target32=false,terminated=false;
 bool debugEnabled=false; std::string debugSourceName; int nextDebugId=6, currentSubprogram=-1;
 std::map<std::tuple<int,int,int>,int> debugLocations; std::ostringstream debugMetadata; std::vector<std::string> breakTargets,continueTargets;
 Type currentRet{"i32",false}; std::map<std::string,std::pair<std::vector<Type>,Type>> signatures;
 std::map<std::string,long long> enumValues; std::map<std::string,std::vector<std::pair<std::string,Type>>> structures;
 std::string next(){return "%t"+std::to_string(temp++);} std::string lbl(const std::string&s){return s+std::to_string(label++);}
 void branch(const std::string& target){if(!terminated){body<<"  br label %"<<target<<"\n";terminated=true;}}
 void startLabel(const std::string& name){branch(name);body<<name<<":\n";terminated=false;}
 static std::string baseName(const std::string&s){size_t p=s.find(' ');return p==std::string::npos?s:s.substr(0,p);}
 std::string castValue(const std::string&v,const Type&from,const Type&to){
  if(from.llvm()==to.llvm())return v;if(to.name=="bool"&&!to.pointer){std::string r=next();body<<"  "<<r<<" = icmp ne "<<from.llvm()<<" "<<v<<", "<<(from.pointer?"null":"0")<<"\n";return r;}if(to.pointer){if(from.pointer)return v;std::string r=next();body<<"  "<<r<<" = inttoptr "<<from.llvm()<<" "<<v<<" to ptr\n";return r;}
  if(from.pointer){std::string r=next();body<<"  "<<r<<" = ptrtoint ptr "<<v<<" to "<<to.llvm()<<"\n";return r;}
  unsigned fb=from.bits(),tb=to.bits();if(fb==tb)return v;std::string r=next();std::string op=fb>tb?"trunc":((from.isUnsigned||from.name=="bool")?"zext":"sext");body<<"  "<<r<<" = "<<op<<" "<<from.llvm()<<" "<<v<<" to "<<to.llvm()<<"\n";return r;
 }
 std::string loadAddress(const std::string&ptr,const Type&t){std::string r=next();body<<"  "<<r<<" = load "<<t.llvm()<<", ptr "<<ptr<<"\n";return r;}
 void guardShift(const Type& amountType,const std::string& amount,unsigned resultBits){
  std::string valid=next(),ok=lbl("shift_valid"),bad=lbl("shift_invalid");
  body<<"  "<<valid<<" = icmp ult "<<amountType.llvm()<<" "<<amount<<", "<<resultBits<<"\n  br i1 "<<valid<<", label %"<<ok<<", label %"<<bad<<"\n"<<bad<<":\n  call void @llvm.trap()\n  unreachable\n"<<ok<<":\n";
 }
 void guardDivision(const Type& type,const std::string& lhs,const std::string& rhs){
  std::string nonzero=next(),safe=lbl("div_nonzero"),zero=lbl("div_zero");
  body<<"  "<<nonzero<<" = icmp ne "<<type.llvm()<<" "<<rhs<<", 0\n  br i1 "<<nonzero<<", label %"<<safe<<", label %"<<zero<<"\n"<<zero<<":\n  call void @llvm.trap()\n  unreachable\n"<<safe<<":\n";
  if(!type.isUnsigned){
   std::string minCmp=next(),minusOne=next(),overflow=next(),ok=lbl("div_safe"),bad=lbl("div_overflow");
   const std::string minimum=type.bits()==64?"-9223372036854775808":type.bits()==32?"-2147483648":type.bits()==16?"-32768":type.bits()==8?"-128":"-1";
   body<<"  "<<minCmp<<" = icmp eq "<<type.llvm()<<" "<<lhs<<", "<<minimum<<"\n  "<<minusOne<<" = icmp eq "<<type.llvm()<<" "<<rhs<<", -1\n  "<<overflow<<" = and i1 "<<minCmp<<", "<<minusOne<<"\n  br i1 "<<overflow<<", label %"<<bad<<", label %"<<ok<<"\n"<<bad<<":\n  call void @llvm.trap()\n  unreachable\n"<<ok<<":\n";
  }
 }
 std::string address(const TypedNode&n){
  if(n.kind=="identifier"){auto i=slots.find(n.text);if(i==slots.end()){error="unknown variable '"+n.text+"'";return "null";}return i->second;}
  if(n.kind=="unary"&&n.text=="*"&&n.children.size()==1)return expr(n.children[0]);
  if(n.kind=="member"&&n.children.size()==1){Type bt=n.children[0].type;std::string ptr;if(n.text.rfind("->",0)==0){ptr=expr(n.children[0]);bt.pointer=false;bt.pointerDepth=0;}else ptr=address(n.children[0]);std::string field=n.text.substr(n.text.find(' ')+1);auto st=structures.find(bt.name.substr(bt.name.find(' ')+1));if(st==structures.end()){error="unknown struct type '"+bt.name+"'";return "null";}for(size_t i=0;i<st->second.size();++i)if(st->second[i].first==field){std::string r=next();body<<"  "<<r<<" = getelementptr inbounds "<<bt.llvm()<<", ptr "<<ptr<<", i32 0, i32 "<<i<<"\n";return r;}error="unknown struct field '"+field+"'";return "null";}
  if(n.kind=="index"&&n.children.size()==2){const auto&base=n.children[0];std::string idx=expr(n.children[1]);Type bt=base.type;std::string ptr;
   if(bt.isArray&&base.kind=="identifier"){ptr=slots[base.text];std::string ok=next(),good=lbl("array_in_bounds"),bad=lbl("array_oob");body<<"  "<<ok<<" = icmp ult "<<n.children[1].type.llvm()<<" "<<idx<<", "<<bt.arraySize<<"\n  br i1 "<<ok<<", label %"<<good<<", label %"<<bad<<"\n"<<bad<<":\n  call void @llvm.trap()\n  unreachable\n"<<good<<":\n";std::string r=next();body<<"  "<<r<<" = getelementptr inbounds ["<<bt.arraySize<<" x "<<Type{bt.name,false}.llvm()<<"], ptr "<<ptr<<", i32 0, i32 "<<idx<<"\n";return r;}
   if(bt.pointer)ptr=expr(base);else {error="index base is not pointer/array";return "null";}std::string r=next();body<<"  "<<r<<" = getelementptr inbounds "<<Type{bt.name,false}.llvm()<<", ptr "<<ptr<<", i32 "<<idx<<"\n";return r;
  }
  error="expression is not an addressable lvalue: "+n.kind+" ["+n.text+"]";return "null";
 }
 std::string expr(const TypedNode&n){
  if(n.kind=="number"){std::string normalized;Type ty;if(parseIntegerLiteral(n.text,normalized,ty))return normalized;error="invalid integer literal '"+n.text+"'";return "0";}
  if(n.kind=="identifier"){if(n.text=="true")return "1";if(n.text=="false")return "0";auto en=enumValues.find(n.text);if(en!=enumValues.end())return std::to_string(en->second);auto v=vars.find(n.text);if(v!=vars.end()&&v->second.isArray){std::string r=next();body<<"  "<<r<<" = getelementptr inbounds ["<<v->second.arraySize<<" x "<<Type{v->second.name,false}.llvm()<<"], ptr "<<slots[n.text]<<", i32 0, i32 0\n";return r;}return loadAddress(address(n),n.type);}
  if(n.kind=="string"){error="string literals are not yet supported by direct IR";return "null";}
  if(n.kind=="member"||n.kind=="index")return loadAddress(address(n),n.type);
  if(n.kind=="unary"&&!n.children.empty()){
   const auto&c=n.children[0];if(n.text=="&")return address(c);
   if(n.text=="++"||n.text=="--"){std::string p=address(c),old=loadAddress(p,c.type),r=next();if(c.type.pointer){body<<"  "<<r<<" = getelementptr "<<Type{c.type.name,false}.llvm()<<", ptr "<<old<<", i32 "<<(n.text=="++"?"1":"-1")<<"\n";}else body<<"  "<<r<<" = "<<(n.text=="++"?"add":"sub")<<" "<<c.type.llvm()<<" "<<old<<", 1\n";body<<"  store "<<c.type.llvm()<<" "<<r<<", ptr "<<p<<"\n";return r;}
   if(n.text=="*")return loadAddress(expr(c),n.type);
   std::string a=expr(c);if(n.text=="+")return a;std::string r=next();if(n.text=="-")body<<"  "<<r<<" = sub "<<n.type.llvm()<<" 0, "<<a<<"\n";else if(n.text=="!"){body<<"  "<<r<<" = icmp eq "<<c.type.llvm()<<" "<<a<<", "<<(c.type.pointer?"null":"0")<<"\n";}else if(n.text=="~")body<<"  "<<r<<" = xor "<<n.type.llvm()<<" "<<a<<", -1\n";else {error="unsupported unary operator '"+n.text+"'";return "0";}return r;
  }
  if(n.kind=="binary"&&n.children.size()==2){const auto&a=n.children[0];const auto&b=n.children[1];std::string op=n.text;if(op==","){expr(a);return expr(b);}
   if(op=="="||op=="+="||op=="-="||op=="*="||op=="/="||op=="%="||op=="&="||op=="|="||op=="^="||op=="<<="||op==">>="){
    std::string ptr=address(a);Type dst=a.type;std::string val=expr(b);if(op!="="){
     Type operationType=commonIntegerType(dst,b.type);
     if(op=="<<="||op==">>="){operationType=dst;if(operationType.bits()<32)operationType=Type{"i32",false};}
     std::string old=castValue(loadAddress(ptr,dst),dst,operationType);
     if(op=="<<="||op==">>=")guardShift(b.type,val,operationType.bits());
     std::string rhs=castValue(val,b.type,operationType);
     std::string ins=op=="+="?"add":op=="-="?"sub":op=="*="?"mul":op=="/="?(operationType.isUnsigned?"udiv":"sdiv"):op=="%="?(operationType.isUnsigned?"urem":"srem"):op=="&="?"and":op=="|="?"or":op=="^="?"xor":op=="<<="?"shl":(operationType.isUnsigned?"lshr":"ashr");
     if(op=="/="||op=="%=")guardDivision(operationType,old,rhs);
     std::string r=next();body<<"  "<<r<<" = "<<ins<<" "<<operationType.llvm()<<" "<<old<<", "<<rhs<<"\n";
     val=castValue(r,operationType,dst);
    }else val=castValue(val,b.type,dst);
    body<<"  store "<<dst.llvm()<<" "<<val<<", ptr "<<ptr<<"\n";return val;
   }
   if((op=="+"||op=="-")&&(a.type.pointer||b.type.pointer)){
    if(a.type.pointer&&!b.type.pointer){std::string p=expr(a),idx=expr(b),r=next();if(op=="-"){std::string neg=next();body<<"  "<<neg<<" = sub "<<b.type.llvm()<<" 0, "<<idx<<"\n";idx=neg;}body<<"  "<<r<<" = getelementptr "<<Type{a.type.name,false}.llvm()<<", ptr "<<p<<", "<<b.type.llvm()<<" "<<idx<<"\n";return r;}
    if(a.type.pointer&&b.type.pointer&&op=="-"){std::string pa=expr(a),pb=expr(b),x=next(),y=next(),d=next(),sizePtr=next(),size=next(),elements=next();Type elem{a.type.name,false};body<<"  "<<x<<" = ptrtoint ptr "<<pa<<" to i64\n  "<<y<<" = ptrtoint ptr "<<pb<<" to i64\n  "<<d<<" = sub i64 "<<x<<", "<<y<<"\n  "<<sizePtr<<" = getelementptr "<<elem.llvm()<<", ptr null, i32 1\n  "<<size<<" = ptrtoint ptr "<<sizePtr<<" to i64\n  "<<elements<<" = sdiv i64 "<<d<<", "<<size<<"\n";return elements;}
    error="invalid pointer arithmetic";return "0";
   }
   Type ct=(op=="<<"||op==">>")?integerPromotion(a.type):commonIntegerType(a.type,b.type);std::string x=castValue(expr(a),a.type,ct),rawY=expr(b);if(op=="<<"||op==">>")guardShift(b.type,rawY,ct.bits());std::string y=castValue(rawY,b.type,ct),r=next(),ins;if(op=="/"||op=="%")guardDivision(ct,x,y);
   if(op=="+")ins="add";else if(op=="-")ins="sub";else if(op=="*")ins="mul";else if(op=="/")ins=ct.isUnsigned?"udiv":"sdiv";else if(op=="%")ins=ct.isUnsigned?"urem":"srem";else if(op=="&")ins="and";else if(op=="|")ins="or";else if(op=="^")ins="xor";else if(op=="<<")ins="shl";else if(op==">>")ins=ct.isUnsigned?"lshr":"ashr";
   else if(op=="=="||op=="!="||op=="<"||op==">"||op=="<="||op==">="){std::string pred=op=="=="?"eq":op=="!="?"ne":op=="<"?(ct.isUnsigned?"ult":"slt"):op==">"?(ct.isUnsigned?"ugt":"sgt"):op=="<="?(ct.isUnsigned?"ule":"sle"):(ct.isUnsigned?"uge":"sge");body<<"  "<<r<<" = icmp "<<pred<<" "<<ct.llvm()<<" "<<x<<", "<<y<<"\n";return r;}
   else if(op=="&&"||op=="||"){std::string cx=next(),cy=next();body<<"  "<<cx<<" = icmp ne "<<ct.llvm()<<" "<<x<<", "<<(ct.pointer?"null":"0")<<"\n  "<<cy<<" = icmp ne "<<ct.llvm()<<" "<<y<<", "<<(ct.pointer?"null":"0")<<"\n  "<<r<<" = "<<(op=="&&"?"and":"or")<<" i1 "<<cx<<", "<<cy<<"\n";return r;}
   else {error="operator '"+op+"' unsupported in direct IR";return "0";}body<<"  "<<r<<" = "<<ins<<" "<<ct.llvm()<<" "<<x<<", "<<y<<"\n";return r;
  }
  if(n.kind=="conditional"&&n.children.size()==3){std::string c=condition(n.children[0]),yes=lbl("cond_yes"),no=lbl("cond_no"),end=lbl("cond_end"),slot=next();allocas<<"  "<<slot<<" = alloca "<<n.type.llvm()<<"\n";body<<"  br i1 "<<c<<", label %"<<yes<<", label %"<<no<<"\n"<<yes<<":\n";terminated=false;std::string a=castValue(expr(n.children[1]),n.children[1].type,n.type);body<<"  store "<<n.type.llvm()<<" "<<a<<", ptr "<<slot<<"\n";branch(end);body<<no<<":\n";terminated=false;std::string b=castValue(expr(n.children[2]),n.children[2].type,n.type);body<<"  store "<<n.type.llvm()<<" "<<b<<", ptr "<<slot<<"\n";branch(end);body<<end<<":\n";terminated=false;return loadAddress(slot,n.type);}
  if(n.kind=="postfix"&&!n.children.empty()){const auto&v=n.children[0];std::string p=address(v),old=loadAddress(p,v.type),r=next();if(v.type.pointer)body<<"  "<<r<<" = getelementptr "<<Type{v.type.name,false}.llvm()<<", ptr "<<old<<", i32 "<<(n.text=="++"?"1":"-1")<<"\n";else body<<"  "<<r<<" = "<<(n.text=="++"?"add":"sub")<<" "<<v.type.llvm()<<" "<<old<<", 1\n";body<<"  store "<<v.type.llvm()<<" "<<r<<", ptr "<<p<<"\n";return old;}
  if(n.kind=="call"){
   if(n.text=="i8"||n.text=="i16"||n.text=="i32"||n.text=="i64"||n.text=="u8"||n.text=="u16"||n.text=="u32"||n.text=="u64"||n.text=="bool"||n.text=="ptr"){if(n.children.size()!=1){error="cast expects one argument";return "0";}return castValue(expr(n.children[0]),n.children[0].type,parseType(n.text));}
   if(n.text=="print"){if(freestanding){error="print is unavailable in freestanding mode";return "0";}if(n.children.size()!=1){error="print expects one argument";return "0";}Type pt=n.children[0].type;std::string v=expr(n.children[0]),fmt,argType;if(pt.pointer){fmt="@.kx_fmt_ptr";argType="ptr";}else if(pt.bits()>32){fmt=pt.isUnsigned?"@.kx_fmt_u64":"@.kx_fmt_i64";argType="i64";v=castValue(v,pt,Type{"i64",false,0,pt.isUnsigned});}else{fmt=pt.isUnsigned?"@.kx_fmt_u32":"@.kx_fmt_i32";argType="i32";v=castValue(v,pt,Type{"i32",false,0,pt.isUnsigned});}body<<"  call i32 (ptr, ...) @printf(ptr "<<fmt<<", "<<argType<<" "<<v<<")\n";return "0";}
   if(n.text=="alloc"||n.text=="arena_alloc"){if(n.children.size()!=1){error=n.text+" expects one size";return "null";}std::string size=expr(n.children[0]);if(freestanding||n.text=="arena_alloc"){std::string size64=castValue(size,n.children[0].type,Type{"i64",false}),off=next(),aligned=next(),newOff=next(),cap=next(),ok=next(),good=lbl("arena_ok"),bad=lbl("arena_overflow"),ptr=next();body<<"  "<<off<<" = load i64, ptr @kx_arena_offset\n  "<<aligned<<" = add i64 "<<off<<", 7\n  ";std::string aligned2=next();body<<aligned2<<" = and i64 "<<aligned<<", -8\n  "<<newOff<<" = add i64 "<<aligned2<<", "<<size64<<"\n  "<<cap<<" = load i64, ptr @kx_arena_capacity\n  "<<ok<<" = icmp ule i64 "<<newOff<<", "<<cap<<"\n  ";std::string noWrap=next(),alignOk=next(),validA=next(),valid=next();body<<noWrap<<" = icmp uge i64 "<<newOff<<", "<<aligned2<<"\n  "<<alignOk<<" = icmp uge i64 "<<aligned<<", "<<off<<"\n  "<<validA<<" = and i1 "<<ok<<", "<<noWrap<<"\n  "<<valid<<" = and i1 "<<validA<<", "<<alignOk<<"\n  br i1 "<<valid<<", label %"<<good<<", label %"<<bad<<"\n"<<bad<<":\n  call void @llvm.trap()\n  unreachable\n"<<good<<":\n  store i64 "<<newOff<<", ptr @kx_arena_offset\n  "<<ptr<<" = getelementptr inbounds [1048576 x i8], ptr @kx_arena, i64 0, i64 "<<aligned2<<"\n";return ptr;}Type sizeType=target32?Type{"i32",false}:Type{"i64",false};std::string sizeArg=castValue(size,n.children[0].type,sizeType),r=next();body<<"  "<<r<<" = call ptr @malloc("<<sizeType.llvm()<<" "<<sizeArg<<")\n";return r;}
   if(n.text=="free"){if(n.children.size()!=1){error="free expects one pointer";return "0";}std::string p=expr(n.children[0]);if(!freestanding)body<<"  call void @free(ptr "<<p<<")\n";return "0";}
   if(n.text=="arena_reset"||n.text=="arena_init"){if(n.text=="arena_init"&&n.children.size()!=1){error="arena_init expects one capacity";return "0";}body<<"  store i64 0, ptr @kx_arena_offset\n";if(n.text=="arena_init"){std::string cap=castValue(expr(n.children[0]),n.children[0].type,Type{"i64",false}),nonneg=next(),cmp=next(),sel=next(),good=lbl("arena_init_ok"),bad=lbl("arena_init_bad");body<<"  "<<nonneg<<" = icmp sge i64 "<<cap<<", 0\n  br i1 "<<nonneg<<", label %"<<good<<", label %"<<bad<<"\n"<<bad<<":\n  call void @llvm.trap()\n  unreachable\n"<<good<<":\n  "<<cmp<<" = icmp ule i64 "<<cap<<", 1048576\n  "<<sel<<" = select i1 "<<cmp<<", i64 "<<cap<<", i64 1048576\n  store i64 "<<sel<<", ptr @kx_arena_capacity\n";}return "0";}
   auto sig=signatures.find(n.text);if(sig==signatures.end()){error="unknown function '"+n.text+"'";return "0";}std::string args;for(size_t i=0;i<n.children.size();++i){if(i)args+=", ";Type pt=sig->second.first[i];args+=pt.llvm()+" "+castValue(expr(n.children[i]),n.children[i].type,pt);}Type rt=sig->second.second;if(rt.name=="void"){body<<"  call void @"<<n.text<<"("<<args<<")\n";return "0";}std::string r=next();body<<"  "<<r<<" = call "<<rt.llvm()<<" @"<<n.text<<"("<<args<<")\n";return r;
  }
  error="expression kind '"+n.kind+"' unsupported in direct IR";return "0";
 }
 bool evalTypedConstant(const TypedNode&n,long long&out){
  if(n.kind=="number"){std::string normalized;Type ty;unsigned long long raw=0;if(!parseIntegerLiteral(n.text,normalized,ty,&raw)||raw>static_cast<unsigned long long>(std::numeric_limits<long long>::max()))return false;out=static_cast<long long>(raw);return true;}
  if(n.kind=="identifier"){auto i=enumValues.find(n.text);if(i==enumValues.end())return false;out=i->second;return true;}
  if(n.kind=="unary"&&n.children.size()==1){long long a=0;if(!evalTypedConstant(n.children[0],a))return false;if(n.text=="-")out=-a;else if(n.text=="+")out=a;else if(n.text=="~")out=~a;else if(n.text=="!")out=!a;else return false;return true;}
  if(n.kind=="binary"&&n.children.size()==2){long long a=0,b=0;if(!evalTypedConstant(n.children[0],a)||!evalTypedConstant(n.children[1],b))return false;if(n.text=="+")out=a+b;else if(n.text=="-")out=a-b;else if(n.text=="*")out=a*b;else if(n.text=="/"){if(!b)return false;out=a/b;}else if(n.text=="%"){if(!b)return false;out=a%b;}else if(n.text=="&")out=a&b;else if(n.text=="|")out=a|b;else if(n.text=="^")out=a^b;else if(n.text=="<<")out=a<<b;else if(n.text==">>")out=a>>b;else return false;return true;}
  return false;
 }
 std::string condition(const TypedNode&n){std::string v=expr(n);if(n.type.name=="bool")return v;if(n.type.name.rfind("struct ",0)==0){error="struct value cannot be used as a condition";return "0";}std::string r=next();body<<"  "<<r<<" = icmp ne "<<n.type.llvm()<<" "<<v<<", "<<(n.type.pointer?"null":"0")<<"\n";return r;}
 void stmtImpl(const TypedNode&n){if(!error.empty()||terminated)return;
  if(n.kind=="block"){auto oldVars=vars;auto oldSlots=slots;for(const auto&c:n.children)stmt(c);vars=oldVars;slots=oldSlots;return;}
  if(n.kind=="declaration"){Type t=parseType(n.text);for(const auto&v:n.children){std::string name=baseName(v.text);Type vt=v.type.name.empty()?t:v.type;bool arr=vt.isArray||v.text.find("[]")!=std::string::npos;if(arr){vt.isArray=true;if(vt.arraySize==0){error="direct IR arrays require a constant size from 1 to 1048576";return;}if(vt.arraySize==0){error="direct IR arrays require a positive constant size";return;}}vars[name]=vt;std::string slot="%v_"+name+"_"+std::to_string(temp++);slots[name]=slot;if(arr)allocas<<"  "<<slot<<" = alloca ["<<vt.arraySize<<" x "<<t.llvm()<<"]\n";else allocas<<"  "<<slot<<" = alloca "<<t.llvm()<<"\n";
    size_t initIndex=arr?1:0;if(arr&&v.children.size()>initIndex&&v.children[initIndex].kind=="initializer_list"){const auto&il=v.children[initIndex];if(il.children.size()>vt.arraySize){error="too many array initializer elements";return;}for(size_t i=0;i<il.children.size();++i){std::string p=next(),x=castValue(expr(il.children[i]),il.children[i].type,t);body<<"  "<<p<<" = getelementptr inbounds ["<<vt.arraySize<<" x "<<t.llvm()<<"], ptr "<<slot<<", i32 0, i32 "<<i<<"\n  store "<<t.llvm()<<" "<<x<<", ptr "<<p<<"\n";}}
    else if(!arr&&!v.children.empty()){std::string x=castValue(expr(v.children[0]),v.children[0].type,t);body<<"  store "<<t.llvm()<<" "<<x<<", ptr "<<slot<<"\n";}else if(!arr)body<<"  store "<<t.llvm()<<" "<<(t.pointer?"null":(t.name.rfind("struct ",0)==0?"zeroinitializer":"0"))<<", ptr "<<slot<<"\n";
   }return;}
  if(n.kind=="return"){if(!n.children.empty()){std::string v=castValue(expr(n.children[0]),n.children[0].type,currentRet);body<<"  ret "<<currentRet.llvm()<<" "<<v<<"\n";}else body<<"  ret void\n";terminated=true;return;}
  if(n.kind=="expression_statement"){if(!n.children.empty())expr(n.children[0]);return;}
  if(n.kind=="if"&&n.children.size()>=2){std::string c=condition(n.children[0]),yes=lbl("if_yes"),no=lbl("if_no"),end=lbl("if_end");body<<"  br i1 "<<c<<", label %"<<yes<<", label %"<<(n.children.size()>2?no:end)<<"\n"<<yes<<":\n";terminated=false;{auto savedVars=vars;auto savedSlots=slots;stmt(n.children[1]);vars=savedVars;slots=savedSlots;}branch(end);if(n.children.size()>2){body<<no<<":\n";terminated=false;{auto savedVars=vars;auto savedSlots=slots;stmt(n.children[2]);vars=savedVars;slots=savedSlots;}branch(end);}body<<end<<":\n";terminated=false;return;}
  if(n.kind=="while"||n.kind=="do_while"||n.kind=="for"){std::string test=lbl("loop_test"),loop=lbl("loop_body"),step=lbl("loop_step"),end=lbl("loop_end");size_t condIndex=n.kind=="do_while"?1:(n.kind=="for"?1:0),bodyIndex=n.kind=="do_while"?0:(n.kind=="for"?3:1);auto loopSavedVars=vars;auto loopSavedSlots=slots;if(n.kind=="for"&&n.children[0].kind!="empty")stmt(n.children[0]);if(n.kind=="do_while")branch(loop);else branch(test);
   if(n.kind!="do_while"){body<<test<<":\n";terminated=false;if(n.kind=="for"&&n.children[1].kind=="empty")branch(loop);else{std::string c=condition(n.children[condIndex]);body<<"  br i1 "<<c<<", label %"<<loop<<", label %"<<end<<"\n";terminated=true;}}
   body<<loop<<":\n";terminated=false;breakTargets.push_back(end);continueTargets.push_back(n.kind=="for"?step:test);{auto savedVars=vars;auto savedSlots=slots;stmt(n.children[bodyIndex]);vars=savedVars;slots=savedSlots;}continueTargets.pop_back();breakTargets.pop_back();branch(n.kind=="for"?step:(n.kind=="do_while"?test:test));
   if(n.kind=="for"){body<<step<<":\n";terminated=false;if(n.children[2].kind!="empty")expr(n.children[2]);branch(test);}
   if(n.kind=="do_while"){body<<test<<":\n";terminated=false;std::string c=condition(n.children[1]);body<<"  br i1 "<<c<<", label %"<<loop<<", label %"<<end<<"\n";terminated=true;}
   body<<end<<":\n";terminated=false;if(n.kind=="for"){vars=loopSavedVars;slots=loopSavedSlots;}return;}
  if(n.kind=="break"){if(breakTargets.empty())error="break outside loop/switch";else branch(breakTargets.back());return;}
  if(n.kind=="continue"){if(continueTargets.empty())error="continue outside loop";else branch(continueTargets.back());return;}
  if(n.kind=="switch"&&n.children.size()>=1){std::string value=expr(n.children[0]),end=lbl("switch_end"),def=end;std::vector<std::string> labs(n.children.size());std::vector<long long> values;std::set<long long> seen;bool hasDefault=false;
   for(size_t i=1;i<n.children.size();++i){const auto&c=n.children[i];if(c.kind=="default"){if(hasDefault){error="duplicate default label in switch";return;}hasDefault=true;def=labs[i]=lbl("switch_default");}else if(c.kind=="case"&&!c.children.empty()){long long v=0;if(!evalTypedConstant(c.children[0],v)){error="switch case must be an integer or enum constant expression";return;}if(!seen.insert(v).second){error="duplicate switch case value";return;}values.push_back(v);labs[i]=lbl("switch_case");}}
   body<<"  switch "<<n.children[0].type.llvm()<<" "<<value<<", label %"<<def<<" [\n";size_t vi=0;for(size_t i=1;i<n.children.size();++i)if(n.children[i].kind=="case")body<<"    "<<n.children[0].type.llvm()<<" "<<values[vi++]<<", label %"<<labs[i]<<"\n";body<<"  ]\n";terminated=true;breakTargets.push_back(end);
   for(size_t i=1;i<n.children.size();++i){const auto&c=n.children[i];std::string lab=labs[i];if(lab.empty())continue;body<<lab<<":\n";terminated=false;auto savedVars=vars;auto savedSlots=slots;size_t first=c.kind=="case"?1:0;for(size_t j=first;j<c.children.size();++j)stmt(c.children[j]);vars=savedVars;slots=savedSlots;std::string nextCase=end;for(size_t k=i+1;k<n.children.size();++k)if(!labs[k].empty()){nextCase=labs[k];break;}branch(nextCase);}
   breakTargets.pop_back();body<<end<<":\n";terminated=false;return;}

  if(n.kind=="legacy_instruction_block"){std::istringstream in(n.text);std::vector<std::string>w;std::string z;while(in>>z){if(z=="-"||z=="+"){std::string magnitude;if(in>>magnitude)z+=magnitude;}if(z==",")continue;while(!z.empty()&&(z.back()==','||z.back()==';'))z.pop_back();if(!z.empty())w.push_back(z);}std::map<std::string,std::string> labels;for(size_t i=0;i<w.size();++i)if(w[i]=="label"&&i+1<w.size()){if(labels.count(w[i+1])){error="duplicate KX label '"+w[i+1]+"'";return;}labels[w[i+1]]=lbl("kx_label");}std::string cmp="0",cmpLeft,cmpRight;Type cmpType{"i32",false};size_t i=0;while(i<w.size()&&!error.size()){std::string op=w[i++];if(op=="label"){if(i>=w.size()){error="label needs name";break;}std::string l=labels[w[i++]];if(!terminated)branch(l);body<<l<<":\n";terminated=false;}
    else if(op=="jmp"){if(i>=w.size()||!labels.count(w[i])){error="jmp to unknown label";break;}branch(labels[w[i++]]);}
    else if(op=="je"||op=="jne"||op=="jg"||op=="jge"||op=="jl"||op=="jle"||op=="jz"||op=="jnz"){if(i>=w.size()||!labels.count(w[i])){error="conditional jump to unknown label";break;}std::string target=labels[w[i++]],fall=lbl("kx_fall"),flag=cmp;if(op!="je"&&op!="jne"){if(cmpLeft.empty()){error="conditional jump requires a preceding cmp";break;}std::string pred=op=="jg"?"sgt":op=="jge"?"sge":op=="jl"?"slt":op=="jle"?"sle":op=="jz"?"eq":"ne";flag=next();body<<"  "<<flag<<" = icmp "<<pred<<" "<<cmpType.llvm()<<" "<<cmpLeft<<", "<<cmpRight<<"\n";}body<<"  br i1 "<<flag<<", label %"<<((op=="jne")?fall:target)<<", label %"<<((op=="jne")?target:fall)<<"\n";terminated=true;body<<fall<<":\n";terminated=false;}
    else if(op=="cmp"){if(i+1>=w.size()){error="cmp needs two operands";break;}std::string a=w[i++],b=w[i++];Type t=vars.count(a)?vars[a]:(vars.count(b)?vars[b]:Type{"i32",false});std::string av,bv;
     if(vars.count(a)){if(t.pointer&&!vars[a].pointer){error="KX cmp pointer type mismatch";break;}av=castValue(loadAddress(slots[a],vars[a]),vars[a],t);}else{std::string norm;Type lit;if(!parseIntegerLiteral(a,norm,lit)){error="invalid KX cmp operand '"+a+"'";break;}if(t.pointer){if(norm!="0"){error="KX pointer cmp requires a pointer or null";break;}av="null";}else av=castValue(norm,lit,t);}
     if(vars.count(b)){if(t.pointer&&!vars[b].pointer){error="KX cmp pointer type mismatch";break;}if(!t.pointer&&vars[b].pointer){error="KX cmp pointer/integer mismatch";break;}if(t.pointer&&vars[b].name!=t.name){error="KX cmp pointer element type mismatch";break;}bv=castValue(loadAddress(slots[b],vars[b]),vars[b],t);}else{std::string norm;Type lit;if(!parseIntegerLiteral(b,norm,lit)){error="invalid KX cmp operand '"+b+"'";break;}if(t.pointer){if(norm!="0"){error="KX pointer cmp requires a pointer or null";break;}bv="null";}else bv=castValue(norm,lit,t);}
     cmpLeft=av;cmpRight=bv;cmpType=t;cmp=next();body<<"  "<<cmp<<" = icmp eq "<<t.llvm()<<" "<<av<<", "<<bv<<"\n";}

    else if(op=="mov"||op=="add"||op=="sub"||op=="mul"||op=="sdiv"||op=="srem"||op=="and"||op=="or"||op=="xor"||op=="shl"||op=="ashr"){if(i+1>=w.size()){error="instruction missing operands";break;}std::string dst=w[i++],src=w[i++];if(!vars.count(dst)){error="unknown KX destination '"+dst+"'";break;}Type t=vars[dst];std::string val;if(op=="mov"){if(t.pointer&&!vars.count(src)&&src=="0")val="null";else if(t.pointer&&!vars.count(src)){error="pointer mov requires another pointer or null";break;}else if(vars.count(src)){if(vars[src].pointer!=t.pointer){error="KX mov type mismatch";break;}val=castValue(loadAddress(slots[src],vars[src]),vars[src],t);}else{std::string norm;Type literalType;if(!parseIntegerLiteral(src,norm,literalType)){error="invalid KX mov operand '"+src+"'";break;}val=castValue(norm,literalType,t);}}else{std::string a=loadAddress(slots[dst],t),r=next();if(t.pointer){if(op!="add"&&op!="sub"){error="only add/sub are valid KX pointer arithmetic";break;}if(vars.count(src)&&vars[src].pointer){error="KX pointer arithmetic requires an integer offset";break;}std::string idx;Type idxType;if(vars.count(src)){idxType=vars[src];if(idxType.pointer||idxType.name.rfind("struct ",0)==0){error="KX pointer offset must be an integer";break;}idx=loadAddress(slots[src],idxType);}else if(!parseIntegerLiteral(src,idx,idxType)){error="invalid KX pointer offset";break;}if(op=="sub"){std::string neg=next();body<<"  "<<neg<<" = sub "<<idxType.llvm()<<" 0, "<<idx<<"\n";idx=neg;}body<<"  "<<r<<" = getelementptr "<<Type{t.name,false}.llvm()<<", ptr "<<a<<", "<<idxType.llvm()<<" "<<idx<<"\n";}else{std::string b;if(vars.count(src)){if(vars[src].pointer){error="integer KX arithmetic cannot use pointer source";break;}b=castValue(loadAddress(slots[src],vars[src]),vars[src],t);}else{std::string norm;Type literalType;if(!parseIntegerLiteral(src,norm,literalType)){error="invalid KX arithmetic operand '"+src+"'";break;}b=castValue(norm,literalType,t);}if(op=="sdiv"||op=="srem"){Type signedType=t;signedType.isUnsigned=false;guardDivision(signedType,a,b);}if(op=="shl"||op=="ashr")guardShift(t,b,t.bits());body<<"  "<<r<<" = "<<op<<" "<<t.llvm()<<" "<<a<<", "<<b<<"\n";}val=r;}body<<"  store "<<t.llvm()<<" "<<val<<", ptr "<<slots[dst]<<"\n";}
    else if(op=="inc"||op=="dec"||op=="neg"||op=="not"){if(i>=w.size()||!vars.count(w[i])){error="unary KX instruction needs variable";break;}std::string a=w[i++];Type t=vars[a];std::string x=loadAddress(slots[a],t),r=next();if(t.pointer&&op!="inc"&&op!="dec"){error="KX neg/not require an integer operand";break;}if((op=="inc"||op=="dec")&&t.pointer)body<<"  "<<r<<" = getelementptr "<<Type{t.name,false}.llvm()<<", ptr "<<x<<", i32 "<<(op=="inc"?"1":"-1")<<"\n";else{std::string ins=op=="inc"?"add":op=="dec"?"sub":op=="neg"?"sub":"xor";body<<"  "<<r<<" = "<<ins<<" "<<t.llvm()<<" "<<(op=="neg"?"0, ":x+", ")<<(op=="not"?"-1":op=="inc"||op=="dec"?"1":x)<<"\n";}body<<"  store "<<t.llvm()<<" "<<r<<", ptr "<<slots[a]<<"\n";}
    else if(op=="print"){if(i>=w.size()){error="print needs operand";break;}TypedNode x;x.kind="identifier";x.text=w[i++];x.type=vars[x.text];TypedNode call;call.kind="call";call.text="print";call.children.push_back(x);expr(call);}
    else if(op=="ret"){if(i<w.size()){if(currentRet.name=="void"){error="void function cannot use ret with a value";break;}std::string token=w[i++],v=token;Type from; if(vars.count(token)){from=vars[token];v=loadAddress(slots[token],from);}else{std::string normalized;if(!parseIntegerLiteral(token,normalized,from)){error="unknown KX ret operand '"+token+"'";break;}v=normalized;}v=castValue(v,from,currentRet);body<<"  ret "<<currentRet.llvm()<<" "<<v<<"\n";}else if(currentRet.name=="void")body<<"  ret void\n";else{error="non-void function requires a value after KX ret";break;}terminated=true;}
    else if(op=="halt"){body<<"  ret "<<currentRet.llvm()<<" "<<(currentRet.name=="void"?"":"0")<<"\n";terminated=true;}
    else if(op=="nop")body<<"  call void asm sideeffect \"nop\", \"\"()\n";
    else {error="unsupported KX instruction '"+op+"'";break;}}
   return;}
  if(n.kind=="asm_block"||n.kind=="asm"){std::string code=n.text;size_t q=code.find('"');if(q!=std::string::npos){size_t e=code.find_last_of('"');if(e>q)code=code.substr(q+1,e-q-1);}for(char&c:code)if(c=='\n')c=' ';body<<"  call void asm sideeffect \""<<code<<"\", \"\"()\n";return;}
  error="statement kind '"+n.kind+"' unsupported in direct IR";
 }
 int debugLocationId(int line, int column) {
  if (!debugEnabled || currentSubprogram < 0) return -1;
  const auto key = std::make_tuple(currentSubprogram, std::max(1, line), std::max(1, column));
  auto found = debugLocations.find(key);
  if (found != debugLocations.end()) return found->second;
  const int id = nextDebugId++;
  debugLocations.emplace(key, id);
  debugMetadata << "!" << id << " = !DILocation(line: " << std::get<1>(key) << ", column: " << std::get<2>(key)
                << ", scope: !" << currentSubprogram << ")\n";
  return id;
 }
 static bool isDebuggableInstruction(const std::string& line) {
  if (line.size() < 3 || line[0] != ' ' || line[1] != ' ') return false;
  const std::string t = line.substr(2);
  if (t.empty() || t.back() == ':') return false;
  return t.find(" = ") != std::string::npos || t.rfind("ret ",0)==0 || t.rfind("br ",0)==0 ||
         t.rfind("call ",0)==0 || t.rfind("store ",0)==0 || t.rfind("switch ",0)==0 ||
         t.rfind("unreachable",0)==0;
 }
 void annotateNewLines(std::ostringstream& stream, const std::string& before, int loc) {
  if (loc < 0) return;
  const std::string all = stream.str();
  if (all.size() <= before.size()) return;
  std::istringstream input(all.substr(before.size()));
  std::ostringstream rewritten;
  std::string line;
  while (std::getline(input, line)) {
   if (isDebuggableInstruction(line) && line.find(", !dbg !") == std::string::npos)
    line += " , !dbg !" + std::to_string(loc);
   rewritten << line << '\n';
  }
  stream.seekp(static_cast<std::streamoff>(before.size()));
  stream << rewritten.str();
 }
 void stmt(const TypedNode& n) {
  if (!debugEnabled) { stmtImpl(n); return; }
  const std::string bodyBefore = body.str();
  const std::string allocasBefore = allocas.str();
  stmtImpl(n);
  if (!error.empty()) return;
  const int loc = debugLocationId(n.line, n.column);
  annotateNewLines(body, bodyBefore, loc);
  annotateNewLines(allocas, allocasBefore, loc);
 }
public:
 Emitter(const Module&m,bool fs,bool p32=false):module(m),freestanding(fs),target32(p32),enumValues(m.enumValues),structures(m.structures){}
 void enableDebugInfo(const std::string& sourceName) { debugEnabled = true; debugSourceName = sourceName; }
 bool emit(std::string&out,std::string&e){
  for(const auto&f:module.functions){std::vector<Type>ps;for(const auto&p:f.params)ps.push_back(p.second);signatures[f.name]={ps,f.result};}
  for(const auto&st:structures){ir<<"%"<<st.first<<" = type { ";for(size_t i=0;i<st.second.size();++i){if(i)ir<<", ";ir<<st.second[i].second.llvm();}ir<<" }\n";}
  if(!freestanding){ir<<"@.kx_fmt_i32 = private unnamed_addr constant [4 x i8] c\"%d\\0A\\00\"\n@.kx_fmt_u32 = private unnamed_addr constant [4 x i8] c\"%u\\0A\\00\"\n@.kx_fmt_i64 = private unnamed_addr constant [6 x i8] c\"%lld\\0A\\00\"\n@.kx_fmt_u64 = private unnamed_addr constant [6 x i8] c\"%llu\\0A\\00\"\n@.kx_fmt_ptr = private unnamed_addr constant [4 x i8] c\"%p\\0A\\00\"\ndeclare i32 @printf(ptr, ...)\ndeclare ptr @malloc("<<(target32?"i32":"i64")<<")\ndeclare void @free(ptr)\n";}
  ir<<"@kx_arena = internal global [1048576 x i8] zeroinitializer, align 16\n@kx_arena_offset = internal global i64 0, align 8\n@kx_arena_capacity = internal global i64 1048576, align 8\ndeclare void @llvm.trap()\n";
  for(const auto&f:module.functions){std::string args,prologue;vars.clear();slots.clear();temp=0;label=0;body.str("");body.clear();allocas.str("");allocas.clear();terminated=false;bool first=true;
   currentSubprogram = -1;
   if (debugEnabled) {
    currentSubprogram = nextDebugId++;
    const int fnLine = std::max(1, f.body.line);
    debugMetadata << "!" << currentSubprogram << " = distinct !DISubprogram(name: \"" << f.name << "\", scope: !1, file: !1, line: " << fnLine << ", type: !3, scopeLine: " << fnLine << ", spFlags: DISPFlagDefinition, unit: !0)\n";
   }
   for(const auto&p:f.params){Type t=p.second;if(!first)args+=", ";first=false;args+=t.llvm()+" %arg_"+p.first;std::string slot="%p_"+p.first;vars[p.first]=t;slots[p.first]=slot;prologue+="  "+slot+" = alloca "+t.llvm()+"\n  store "+t.llvm()+" %arg_"+p.first+", ptr "+slot+"\n";}
   currentRet=f.result;if(f.body.kind!="block"){e="function missing body";return false;}stmt(f.body);if(!error.empty()){e=error;return false;}std::string b=body.str();if(!terminated){if(currentRet.name=="void")b+="  ret void\n";else b+="  ret "+currentRet.llvm()+" 0\n";}
   std::string allocText = allocas.str();
   if (debugEnabled) {
    const int entryLoc = debugLocationId(f.body.line, f.body.column);
    std::istringstream prologueInput(prologue); std::ostringstream prologueOutput; std::string prologueLine;
    while (std::getline(prologueInput, prologueLine)) { if (entryLoc >= 0 && isDebuggableInstruction(prologueLine)) prologueLine += " , !dbg !" + std::to_string(entryLoc); prologueOutput << prologueLine << "\n"; }
    prologue = prologueOutput.str();
   }
   ir<<"define "<<currentRet.llvm()<<" @"<<(freestanding&&f.name=="main"?"kx_kernel_main":f.name)<<"("<<args<<")";
   if (debugEnabled && currentSubprogram >= 0) ir << " !dbg !" << currentSubprogram;
   ir << " {\nentry:\n"<<allocText<<prologue<<b<<"}\n";
  }
  if (debugEnabled) {
   std::ostringstream metadata;
   metadata << "!llvm.dbg.cu = !{!0}\n!llvm.module.flags = !{!4, !5}\n"
            << "!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: \"KX Compiler v0.21\", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)\n"
            << "!1 = !DIFile(filename: \"";
   for (char c : debugSourceName) { if (c == '\\' || c == '\"') metadata << '\\'; if (static_cast<unsigned char>(c) >= 32) metadata << c; }
   metadata << "\", directory: \".\")\n!2 = !{}\n!3 = !DISubroutineType(types: !2)\n"
            << "!4 = !{i32 2, !\"Debug Info Version\", i32 3}\n!5 = !{i32 2, !\"Dwarf Version\", i32 4}\n"
            << debugMetadata.str();
   out = ir.str() + metadata.str();
  } else out=ir.str();
  return true;
 }
};
}
