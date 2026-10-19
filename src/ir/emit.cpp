#include "ir/emit.hpp"
#include <iomanip>
#include <sstream>
#include <map>

namespace lab::ir {
using namespace ast;
using sema::Type;
namespace {
struct Module;
struct Function {
    Module& module;
    std::ostringstream code;
    int next=0;
    bool terminated=false;
    explicit Function(Module& m):module(m){}
    std::string temp(){return "%t"+std::to_string(next++);}
    std::string label(){return "b"+std::to_string(next++);}
    void line(const std::string& s){code<<"  "<<s<<'\n';}
    void branch(const std::string& l){if(!terminated)line("br label %"+l);terminated=true;}
    void block(const std::string& l){code<<l<<":\n";terminated=false;}
    std::string assign(const std::string& s){auto v=temp();line(v+" = "+s);return v;}
    std::string call(const std::string& name,const std::vector<std::string>& args){std::string s="call i64 @"+name+"(";for(std::size_t i=0;i<args.size();++i){if(i)s+=", ";s+="i64 "+args[i];}return assign(s+")");}
    void store(int id,const std::string& v){line("store i64 "+v+", i64* %v"+std::to_string(id));}
    std::string load(int id){return assign("load i64, i64* %v"+std::to_string(id));}
    std::string copy(std::string v,Type t){return t.composite()?call("lab_clone",{v}):v;}
    void allocas();
    std::string literal(const std::string&);
    std::string zero(Type);
    std::string expr(const Expr&);
    void stmt(const Stmt&);
    void body(const BlockStmt& b){for(auto& s:b.statements){if(terminated)break;stmt(*s);}}
    std::pair<std::string,std::string> address(const Expr&);
};
struct Module {
    const sema::Checked& checked;
    std::ostringstream globals,functions;
    int string_count=0,map_count=0;
    explicit Module(const sema::Checked& c):checked(c){}
    Type type(const Expr& e)const{return checked.types.at(&e);}
    std::string callback(const ParallelMapExpr& m) {
        auto name="map_"+std::to_string(map_count++); Function f(*this);
        f.code<<"define internal i64 @"<<name<<"(i64 %index, i64 %element, i64 %environment) uwtable {\nentry:\n";
        f.allocas(); auto ids=checked.map_bindings.at(&m); f.store(ids.first,"%index"); f.store(ids.second,"%element");
        auto& captures=checked.captures.at(&m);
        for(std::size_t i=0;i<captures.size();++i) f.store(captures[i],f.call("lab_get",{"%environment",std::to_string(i)}));
        f.body(*m.body); auto value=f.copy(f.expr(*m.result),type(*m.result)); f.line("ret i64 "+value); f.code<<"}\n";
        functions<<f.code.str();return name;
    }
};
void Function::allocas(){for(auto& v:module.checked.variables)line("%v"+std::to_string(v.first)+" = alloca i64");}
std::string Function::literal(const std::string& s){
    auto name="str_"+std::to_string(module.string_count++);
    module.globals<<"@"<<name<<" = private unnamed_addr constant ["<<s.size()+1<<" x i8] c\"";
    for(unsigned char c:s) module.globals<<'\\'<<std::hex<<std::setw(2)<<std::setfill('0')<<int(c);
    module.globals<<"\\00\"\n"<<std::dec;
    auto pointer=assign("getelementptr ["+std::to_string(s.size()+1)+" x i8], ["+std::to_string(s.size()+1)+" x i8]* @"+name+", i64 0, i64 0");
    return assign("call i64 @lab_string(i8* "+pointer+", i64 "+std::to_string(s.size())+")");
}
std::string Function::zero(Type t){
    if(t.array)return call("lab_object",{"0"});
    if(t.name=="string")return literal("");
    if(!t.composite())return "0";
    auto s=module.checked.structures.at(t.name); auto result=call("lab_object",{std::to_string(s->fields.size())});
    for(std::size_t i=0;i<s->fields.size();++i){auto& f=s->fields[i];Type ft{f.type.name,f.type.array};auto v=zero(ft);call("lab_set",{result,std::to_string(i),v,ft.composite()?"1":"0"});}
    return result;
}
std::pair<std::string,std::string> Function::address(const Expr& e){
    if(e.kind==Kind::Index){auto& i=static_cast<const IndexExpr&>(e);auto b=expr(*i.base);auto index=expr(*i.index); // Check before evaluating the assignment RHS.
        call("lab_get",{b,index});return {b,index};}
    auto& m=static_cast<const MemberExpr&>(e);auto b=expr(*m.base);auto s=module.checked.structures.at(module.type(*m.base).name);
    std::size_t index=0;while(s->fields[index].name!=m.member)++index;return {b,std::to_string(index)};
}
std::string Function::expr(const Expr& e){
    switch(e.kind){
    case Kind::Integer: {auto v=static_cast<const IntegerExpr&>(e).magnitude;auto p=v.find_first_not_of('0');return p==std::string::npos?"0":v.substr(p);}
    case Kind::Boolean:return static_cast<const BooleanExpr&>(e).value?"1":"0";
    case Kind::String:return literal(static_cast<const StringExpr&>(e).value);
    case Kind::Name:return load(module.checked.bindings.at(&e));
    case Kind::Unary:{auto& u=static_cast<const UnaryExpr&>(e);
        if(u.op=="-"&&u.operand->kind==Kind::Integer){auto s=static_cast<const IntegerExpr&>(*u.operand).magnitude;auto p=s.find_first_not_of('0');if(p!=std::string::npos&&s.substr(p)=="9223372036854775808")return "-9223372036854775808";}
        auto v=expr(*u.operand);if(u.op=="-")return call("lab_int_neg",{v});if(u.op=="!")return assign("xor i64 "+v+", 1");return v;
    }
    case Kind::Binary:{auto& b=static_cast<const BinaryExpr&>(e);auto left=expr(*b.left);
        if(b.op=="&&"||b.op=="||"){
            auto rhs=label(),short_path=label(),merge=label(); auto test=assign("icmp ne i64 "+left+", 0");
            line("br i1 "+test+", label %"+(b.op=="&&"?rhs:short_path)+", label %"+(b.op=="&&"?short_path:rhs));
            block(short_path);auto short_end=label();branch(short_end);block(short_end);branch(merge);
            block(rhs);auto right=expr(*b.right);auto right_end=label();branch(right_end);block(right_end);branch(merge);
            block(merge);return assign("phi i64 [ "+std::string(b.op=="&&"?"0":"1")+", %"+short_end+" ], [ "+right+", %"+right_end+" ]");
        }
        auto right=expr(*b.right);
        std::map<std::string,std::string> ops={{"+","0"},{"-","1"},{"*","2"},{"/","3"},{"%","4"}};
        if(ops.count(b.op))return call("lab_int_op",{ops.at(b.op),left,right});
        if(module.type(*b.left).name=="string"){auto eq=call("lab_equal",{left,right});return b.op=="=="?eq:assign("xor i64 "+eq+", 1");}
        std::map<std::string,std::string> cmp={{"==","eq"},{"!=","ne"},{"<","slt"},{"<=","sle"},{">","sgt"},{">=","sge"}};
        auto test=assign("icmp "+cmp.at(b.op)+" i64 "+left+", "+right);return assign("zext i1 "+test+" to i64");
    }
    case Kind::Cast:{auto& c=static_cast<const CastExpr&>(e);auto v=expr(*c.value);auto from=module.type(*c.value),to=module.type(e);
        if(from==to)return v;
        if(to.name=="int")return call("lab_to_int",{v});
        return call("lab_to_string",{v,from.name=="bool"?"1":"0"});}
    case Kind::Index:case Kind::Member:{auto a=address(e);return call("lab_get",{a.first,a.second});}
    case Kind::Array:{auto& a=static_cast<const ArrayExpr&>(e);auto result=call("lab_object",{std::to_string(a.elements.size())});
        for(std::size_t i=0;i<a.elements.size();++i){auto t=module.type(*a.elements[i]);auto v=copy(expr(*a.elements[i]),t);call("lab_set",{result,std::to_string(i),v,t.composite()?"1":"0"});}return result;}
    case Kind::NewArray:{auto& a=static_cast<const NewArrayExpr&>(e);auto n=expr(*a.length);Type t{a.element_type.name};auto v=zero(t);return call("lab_array",{n,v,t.composite()?"1":"0"});}
    case Kind::Call:{auto& c=static_cast<const CallExpr&>(e);auto name=static_cast<const NameExpr&>(*c.callee).name;
        std::vector<std::string> args;for(auto& a:c.arguments)args.push_back(copy(expr(*a),module.type(*a)));
        if(module.checked.functions.count(name))return call("user_"+name,args);
        if(module.checked.structures.count(name)){
            auto result=call("lab_object",{std::to_string(args.size())});for(std::size_t i=0;i<args.size();++i)call("lab_set",{result,std::to_string(i),args[i],module.type(*c.arguments[i]).composite()?"1":"0"});return result;
        }
        if(name=="range"){
            if(args.size()==1)args={"0",args[0],"1"};else if(args.size()==2)args.push_back("1");return call("lab_range",args);
        }
        if(name=="len")return call(module.type(*c.arguments[0]).array?"lab_length":"lab_strlen",args);
        return call("lab_"+name,args);
    }
    case Kind::ParallelMap:{auto& m=static_cast<const ParallelMapExpr&>(e);auto input=expr(*m.input);
        auto& captures=module.checked.captures.at(&m);auto env=call("lab_object",{std::to_string(captures.size())});
        for(std::size_t i=0;i<captures.size();++i)call("lab_set",{env,std::to_string(i),load(captures[i]),module.checked.variables.at(captures[i]).composite()?"1":"0"});
        auto callback=module.callback(m);Type element{module.type(e).name};
        return assign("call i64 @lab_parallel(i64 "+input+", i64 "+env+", i64 (i64, i64, i64)* @"+callback+", i64 "+(element.composite()?"1":"0")+")");
    }
    default:throw std::logic_error("unhandled checked expression");
    }
}
void Function::stmt(const Stmt& s){
    switch(s.kind){
    case Kind::Empty:break;
    case Kind::Block:body(static_cast<const BlockStmt&>(s));break;
    case Kind::Variable:{auto& v=static_cast<const VariableStmt&>(s);if(v.initializer)store(module.checked.bindings.at(&s),copy(expr(*v.initializer),module.type(*v.initializer)));break;}
    case Kind::Assignment:{auto& a=static_cast<const AssignmentStmt&>(s);
        if(a.target->kind==Kind::Name)store(module.checked.bindings.at(a.target.get()),copy(expr(*a.value),module.type(*a.value)));
        else{auto address_value=address(*a.target);auto t=module.type(*a.value);auto v=copy(expr(*a.value),t);call("lab_set",{address_value.first,address_value.second,v,t.composite()?"1":"0"});}break;}
    case Kind::Expression:expr(*static_cast<const ExpressionStmt&>(s).expression);break;
    case Kind::Return:{auto& r=static_cast<const ReturnStmt&>(s);auto v=r.value?copy(expr(*r.value),module.type(*r.value)):"0";line("ret i64 "+v);terminated=true;break;}
    case Kind::If:{auto& i=static_cast<const IfStmt&>(s);auto done=label();for(auto& b:i.branches){auto yes=label(),no=label();auto v=expr(*b.condition);auto test=assign("icmp ne i64 "+v+", 0");line("br i1 "+test+", label %"+yes+", label %"+no);block(yes);body(*b.body);branch(done);block(no);}if(i.else_body)body(*i.else_body);branch(done);block(done);break;}
    case Kind::While:{auto& w=static_cast<const WhileStmt&>(s);auto condition=label(),loop=label(),done=label();branch(condition);block(condition);auto v=expr(*w.condition);auto test=assign("icmp ne i64 "+v+", 0");line("br i1 "+test+", label %"+loop+", label %"+done);block(loop);body(*w.body);branch(condition);block(done);break;}
    case Kind::For:{auto& f=static_cast<const ForStmt&>(s);if(f.initializer)stmt(*f.initializer);auto condition=label(),loop=label(),done=label();branch(condition);block(condition);
        if(f.condition){auto v=expr(*f.condition);auto test=assign("icmp ne i64 "+v+", 0");line("br i1 "+test+", label %"+loop+", label %"+done);}else branch(loop);
        block(loop);body(*f.body);if(!terminated&&f.step)stmt(*f.step);branch(condition);block(done);break;}
    case Kind::RangeFor:{auto& f=static_cast<const RangeForStmt&>(s);auto array=expr(*f.iterable);auto length=call("lab_length",{array});
        auto slot=temp();line(slot+" = alloca i64");line("store i64 0, i64* "+slot);auto condition=label(),loop=label(),done=label();branch(condition);block(condition);
        auto index=assign("load i64, i64* "+slot);auto test=assign("icmp slt i64 "+index+", "+length);line("br i1 "+test+", label %"+loop+", label %"+done);
        block(loop);auto value=call("lab_get",{array,index});store(module.checked.bindings.at(&s),copy(value,{f.type.name,f.type.array}));body(*f.body);
        if(!terminated){auto next_index=assign("add i64 "+index+", 1");line("store i64 "+next_index+", i64* "+slot);branch(condition);}block(done);break;}
    default:throw std::logic_error("unhandled checked statement");
    }
}
}
std::string emit(const Program&,const sema::Checked& checked){
    Module module(checked);
    for(auto& item:checked.functions){auto f=item.second;Function function(module);function.code<<"define i64 @user_"<<f->name<<'(';
        auto& ids=checked.parameters.at(f);for(std::size_t i=0;i<ids.size();++i){if(i)function.code<<", ";function.code<<"i64 %arg"<<i;}
        function.code<<") uwtable {\nentry:\n";function.allocas();for(std::size_t i=0;i<ids.size();++i)function.store(ids[i],"%arg"+std::to_string(i));
        function.body(*f->body);if(!function.terminated)function.line("ret i64 0");function.code<<"}\n";module.functions<<function.code.str();
    }
    std::ostringstream out;
    out<<"; CompilerLab checked AST -> LLVM IR. Runtime values use the i64 ABI.\n";
    out<<"declare i64 @lab_string(i8*, i64)\ndeclare i64 @lab_parallel(i64, i64, i64 (i64, i64, i64)*, i64)\n";
    const std::map<std::string,int> runtime={{"lab_int_neg",1},{"lab_int_op",3},{"lab_concat",2},{"lab_equal",2},{"lab_substr",3},{"lab_strlen",1},{"lab_print",1},{"lab_read_char",0},{"lab_to_string",2},{"lab_to_int",1},{"lab_object",1},{"lab_clone",1},{"lab_get",2},{"lab_set",4},{"lab_length",1},{"lab_array",3},{"lab_range",3}};
    for(auto& f:runtime){out<<"declare i64 @"<<f.first<<'(';for(int i=0;i<f.second;++i){if(i)out<<", ";out<<"i64";}out<<")\n";}
    out<<module.globals.str()<<module.functions.str()<<"define i64 @lab_entry() uwtable {\nentry:\n  %result = call i64 @user_main()\n  ret i64 %result\n}\n";return out.str();
}
}
