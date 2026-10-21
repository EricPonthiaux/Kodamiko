class Invoice:
    def total(self, tax, rounded=False):
        return tax


def bill(invoice: Invoice):
    return invoice.total(0.2, True)
