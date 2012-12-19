#pragma once

#include <memory>

class TargetWindow;

// CleartypeAlphaBlendingDialog dialog
class CleartypeAlphaBlendingDialog : public CDialogEx
{
// Construction
public:
    CleartypeAlphaBlendingDialog(CWnd* pParent = NULL);    // standard constructor

// Dialog Data
    enum { IDD = IDD_CLEARTYPE_ALPHA_BLENDING_DIALOG };

    protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support


// Implementation
protected:
    HICON m_hIcon;

    // Generated message map functions
    virtual BOOL OnInitDialog();
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    DECLARE_MESSAGE_MAP()

private:
    std::unique_ptr<TargetWindow> target_window_;
};