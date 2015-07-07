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
    virtual void run(MatchFinder::MatchResult const& result)
    {
        auto stringLiteral = result.Nodes.getNodeAs<StringLiteral>("name");
        auto method = result.Nodes.getNodeAs<CXXMethodDecl>("method");

        auto name = stringLiteral->getString().str();
        auto parentRecord = method->getParent();

        std::cout << "Bound name: " << stringLiteral->getString().str() << "\n";
        std::cout << "Bound function: " << method->getNameAsString() << "\n";
        std::cout << "\tParent: " << parentRecord->getNameAsString() << "\n";
        std::cout << "\tReturn: " << method->getReturnType().getAsString() << "\n";
        for (auto param : method->params())
        {
            std::cout   << "\tParameter: " << param->getType().getAsString() 
                        << " " << param->getNameAsString()
                        << "\n";
        }
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
