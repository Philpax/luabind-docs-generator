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

class DefHandler : public MatchFinder::MatchCallback
{
public:
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

    QualType transformType(QualType type)
    {
        type = removeSmartPointer(type);
        type = type.getNonReferenceType();
        type = type.getUnqualifiedType();
        return type;
    }

    virtual void run(MatchFinder::MatchResult const& result)
    {
        auto stringLiteral = result.Nodes.getNodeAs<StringLiteral>("name");
        auto method = result.Nodes.getNodeAs<CXXMethodDecl>("method");

        auto name = stringLiteral->getString().str();
        auto parentRecord = method->getParent();

        auto returnType = transformType(method->getReturnType());

        std::cout   << returnType.getAsString() << " "
                    << parentRecord->getNameAsString() << ":"
                    << name << "(";

        bool first = true;
        for (auto param : method->params())
        {
            auto type = transformType(param->getType());

            if (!first)
                std::cout << ", ";

            auto typeString = type.getAsString();
            if (typeString.find("lua_State") != std::string::npos)
                continue;

            std::cout << type.getAsString() << " " << param->getNameAsString();
            first = false;
        }

        std::cout << ")\n";
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

    return Tool.run(newFrontendActionFactory(&finder).get());
}
