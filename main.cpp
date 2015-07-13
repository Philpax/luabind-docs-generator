#include "clang/AST/AST.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include <string>
#include <iostream>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

struct LuabindFunction
{
    Type const* returnType;
    std::string name;
    std::vector<ParmVarDecl*> arguments;
};

struct LuabindClass
{
    std::string name;
    std::vector<LuabindFunction> memberFunctions;
    std::vector<LuabindFunction> staticFunctions;
};

std::map<Type const*, LuabindClass> classes;

QualType removeSmartPointer(QualType type)
{
    auto record =
        dyn_cast_or_null<ClassTemplateSpecializationDecl>(type->getAsCXXRecordDecl());

    if (record)
    {
        // If we're dealing with a smart pointer, replace the return type
        // with the original type
        if (record->getNameAsString().find("_ptr") != std::string::npos)
            type = record->getTemplateArgs()[0].getAsType();
    }

    return type;
}

Type const* transformType(QualType type)
{
    type = removeSmartPointer(type);
    type = type.getNonReferenceType();
    type = type.getUnqualifiedType();

    if (auto pointerType = dyn_cast<PointerType>(type.getTypePtr()))
        type = QualType(transformType(pointerType->getPointeeType()), 0);

    return type.getCanonicalType().getTypePtr();
}

class DefHandler : public MatchFinder::MatchCallback
{
public:
    virtual void run(MatchFinder::MatchResult const& result)
    {
        auto classConstruct = result.Nodes.getNodeAs<CXXConstructExpr>("classConstruct");
        auto className = result.Nodes.getNodeAs<StringLiteral>("className");
        auto name = result.Nodes.getNodeAs<StringLiteral>("name");
        auto method = result.Nodes.getNodeAs<CXXMethodDecl>("method");

        // The method we're binding may not actually come from the class,
        // so extract the class from the lb::class_ constructor
        auto classInstance = 
            dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                classConstruct->getConstructor()->getParent());

        auto parentType = classInstance->getTemplateArgs()[0].getAsType().getTypePtr();

        if (classes.find(parentType) == classes.end())
            classes[parentType].name = className->getString().str();

        LuabindFunction fn;
        fn.returnType = transformType(method->getReturnType());
        fn.name = name->getString().str();

        for (auto param : method->params())
            fn.arguments.push_back(param);

        classes[parentType].memberFunctions.push_back(fn);
    }
};

int main(int argc, const char** argv)
{
    llvm::cl::OptionCategory luabindDocsGeneratorCategory("Luabind Docs Generator");
    CommonOptionsParser op(argc, argv, luabindDocsGeneratorCategory);
    ClangTool Tool(op.getCompilations(), op.getSourcePathList());

    // match lb::class_<*>("*").def("name", &C::F)
    auto defMatcher =
        memberCallExpr
        (
            // Match the class_ constructor
            hasDescendant
            (
                constructExpr
                (
                    hasAnyArgument
                    (
                        ignoringImpCasts
                        (
                            stringLiteral().bind("className")
                        )
                    )
                ).bind("classConstruct")
            ),
            // Match class_ member calls
            on
            (
                hasType
                (
                    recordDecl
                    (
                        hasName("class_")
                    )
                )
            ),
            // Match methods with "def"
            callee
            (
                methodDecl
                (
                    hasName("def")
                )
            ),
            // Match "name"
            hasArgument
            (
                0,
                ignoringImpCasts
                (
                    stringLiteral().bind("name")
                )
            ),
            // Match &C::F
            hasArgument
            (
                1,
                unaryOperator
                (
                    hasUnaryOperand
                    (
                        declRefExpr
                        (
                            hasDeclaration
                            (
                                methodDecl().bind("method")
                            )
                        )
                    )
                )
            )
        ).bind("def");

    DefHandler defHandler;
    MatchFinder finder;
    finder.addMatcher(defMatcher, &defHandler);

    auto ret = Tool.run(newFrontendActionFactory(&finder).get());

    auto getTypeName = [&](Type const* type)
    {
        if (classes.find(type) != classes.end())
            return classes[type].name;
        else
            return QualType(type, 0).getAsString();
    };

    for (auto pair : classes)
    {
        auto& klass = pair.second;
        std::cout << klass.name << "\n";
        std::sort(klass.memberFunctions.begin(), klass.memberFunctions.end(),
            [&](LuabindFunction const& a, LuabindFunction const& b)
            {
                return a.name < b.name;
            });

        for (auto& memberFn : klass.memberFunctions)
        {
            std::cout << "\t";
            std::cout << getTypeName(memberFn.returnType) << " ";
            std::cout << memberFn.name;
            std::cout << "(";

            bool first = true;
            for (auto param : memberFn.arguments)
            {
                auto type = transformType(param->getType());

                if (!first)
                    std::cout << ", ";

                std::string typeString = getTypeName(type);
                if (typeString.find("lua_State") != std::string::npos)
                    continue;

                std::cout << typeString << " " << param->getNameAsString();
                first = false;
            }

            std::cout << ")\n";
        }
    }

    return ret;
}
